static void verify_assembly_history_cursor(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    std::filesystem::create_directories(directory);
    auto part=document::PartDocument::create_default();
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,10,10));
    auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
    feature.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;feature.feature.sides[0].length=10;
    part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Stock"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    const auto calculated=kernel::OcctKernel{}.evaluate_history(part.kernel_operations());
    const auto source=directory/"history-source.prtz";part.save(source,calculated);
    auto model=assembly::AssemblyDocument::create_default();
    auto first=assembly::AssemblyDocument::create_part_occurrence("First",part.document_id,source,calculated.back());
    auto second=assembly::AssemblyDocument::create_part_occurrence("Second",part.document_id,source,calculated.back());second.placement.x=20;
    const auto a=first.occurrence_id,b=second.occurrence_id;model.components={first,second};model.synchronize_history();
    const auto path=directory/"assembly-history.asmz";model.save(path);
    window.resize(1366,900);window.show();check(window.open_document_path(QString::fromStdWString(path.wstring())),"History Assembly cannot open");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"History workspace missing");
    const auto row=[&](const std::string& id,const char* role=nullptr)->QTreeWidgetItem* {
        for(QTreeWidgetItemIterator it(tree);*it;++it)if(role?(*it)->data(0,Qt::UserRole+3).toString()==role:
            (*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;
        return nullptr;
    };
    const auto cursor=[&]{const auto answer=window.execute_console_command("history.list");check(answer.ok,"History query failed");return answer.data.at("cursor").get<std::size_t>();};
    const auto drag=[&](QPoint target) {
        auto* marker=row({},"assembly-insert-here");check(marker,"Assembly marker missing");tree->scrollToItem(marker);flush();
        const auto start=tree->visualItemRect(marker).center();
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,QEvent::MouseButtonRelease}) {
            const auto position=type==QEvent::MouseButtonPress?start:target;
            QMouseEvent event(type,QPointF(position),QPointF(tree->viewport()->mapToGlobal(position)),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&event);flush();
        }
    };
    check(cursor()==2&&row(a)&&row(b),"Initial history order missing");
    drag(tree->visualItemRect(row(a)).topLeft()+QPoint(80,1));check(cursor()==0,"Assembly drag did not rewind history");
    check(std::ranges::none_of(view->mesh().triangle_references,[](const auto& face){return !face.instance_path.empty();}),"Rewound Assembly still displays components");
    check(row(a)->font(0).italic()&&row(b)->font(0).italic(),"Inactive history rows are not distinguished");
    auto* last=row(b);drag(tree->visualItemRect(last).bottomLeft()+QPoint(80,4));check(cursor()==2,"Assembly drag did not restore history");
    check(std::ranges::any_of(view->mesh().triangle_references,[](const auto& face){return !face.instance_path.empty();}),"Forward cursor lost component geometry");
    const auto answer=window.execute_console_command("history.cursor 1");check(answer.ok,"History cursor API failed");flush();
    check(cursor()==1,"History cursor API and GUI disagree");
    check(window.execute_console_command("save").ok,"History save failed");
    const auto reopened=assembly::AssemblyDocument::load(path,{},false);
    check(reopened.effective_history_cursor()==1&&reopened.history_order[0].id==a&&reopened.history_order[1].id==b,"GUI native save lost history");
    // Keep a separate component as passive top-level context while rewinding
    // the exact active nested Assembly occurrence.
    check(window.open_document_path(QString::fromStdWString(source.wstring())),"Passive Part cannot open");flush();
    check(window.execute_console_command("new assembly history-parent").ok,"Parent creation failed");
    const auto outer=window.execute_console_command(QString::fromStdString("component.insert "+model.document_id));
    check(outer.ok,"Nested Assembly insertion failed");
    const auto passive=window.execute_console_command(QString::fromStdString("component.insert "+part.document_id));
    check(passive.ok,"Passive context insertion failed");
    const auto outer_id=outer.data.at("occurrence").get<std::string>();
    const auto passive_id=passive.data.at("occurrence").get<std::string>();
    const auto nested_path=assembly::InstancePath{}.child(outer_id).encoded();
    check(window.execute_console_command(QString::fromStdString("component.activate "+nested_path)).ok,"Nested activation failed");flush();
    check(row({},"assembly-insert-here"),"Active nested Assembly marker missing");
    for(auto* ancestor=row(a)->parent();ancestor;ancestor=ancestor->parent())ancestor->setExpanded(true);
    flush();
    drag(tree->visualItemRect(row(a)).topLeft()+QPoint(80,1));check(cursor()==0,"Nested drag did not rewind local history");
    const auto has_path=[&](const std::string& path){return std::ranges::any_of(view->mesh().triangle_references,
        [&](const auto& face){return face.instance_path==path;});};
    check(!has_path(assembly::InstancePath::decode(nested_path).child(a).encoded())&&
        has_path(assembly::InstancePath{}.child(passive_id).encoded()),"Nested cursor lost parent context or retained inactive child");
    check(window.execute_console_command("history.cursor 2").ok,"Nested forward cursor failed");flush();
    check(has_path(assembly::InstancePath::decode(nested_path).child(a).encoded())&&
        has_path(assembly::InstancePath{}.child(passive_id).encoded()),"Nested forward cursor did not restore exact occurrence");
    std::cout<<"Assembly cursor GUI: actual rewind/forward drags, cached display, Tree state, command API and native save passed\n";
}
