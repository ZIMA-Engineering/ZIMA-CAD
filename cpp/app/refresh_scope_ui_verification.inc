int verify_refresh_scope(QApplication& application, zima::app::AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    using namespace zima;
    auto part=document::PartDocument::create_default();
    part.history.push_back(test::rectangular_feature(part,{10,10,10}));
    kernel::OcctKernel kernel;
    const auto bodies=kernel.evaluate_history(part.kernel_operations());
    const auto part_path=directory/"refresh-scope.prtz";
    part.save(part_path,bodies);
    const kernel::BodySnapshot source=bodies.back();
    auto assembly=assembly::AssemblyDocument::create_default();
    for(int i=0;i<256;++i) {
        auto item=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_path,source);
        item.placement.x=(i%32)*15.;item.placement.y=(i/32)*15.;
        assembly.components.push_back(std::move(item));
    }
    auto& first=assembly.components.front();
    first.placement.z=5;
    first.placement_references.push_back({assembly::MateKind::PlaneCoincident,
        {assembly::MateReferenceKind::Face,assembly::InstancePath{}.child(first.occurrence_id),part.document_id+":origin","origin:plane:xy"},
        {assembly::MateReferenceKind::Face,{},assembly.document_id+":origin","origin:plane:xy"},5});
    const auto path=directory/"refresh-scope.asmz";
    assembly.save(path);
    window.resize(1100,850);window.show();application.processEvents();
    if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open refresh-scope Assembly"))return 1;
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* filter=window.findChild<QComboBox*>("selectionFilterCombo");
    auto* selection=window.findChild<QAction*>("viewSelectionAction");
    if(!verify(view&&tree&&filter&&selection,"Refresh-scope controls missing"))return 1;
    if(!verify(view->mesh().triangles.size()>=256*12*3,"Refresh fixture lost source geometry"))return 1;
    auto camera=view->camera_state();camera[4]*=1.2F;camera[5]=17;camera[6]=-23;
    view->set_camera_state(camera);
    int resets=0;
    QObject observer;
    QObject::connect(tree->model(),&QAbstractItemModel::modelReset,&observer,[&]{++resets;});
    const auto measure=[&](const char* name,const auto& action,bool lightweight=true) {
        const auto revision=view->base_mesh_revision();const auto before=resets;
        const auto camera_before=view->camera_state();
        const QPersistentModelIndex root(tree->model()->index(0,0));
        const auto scroll=tree->verticalScrollBar()->value();
        const auto start=std::chrono::steady_clock::now();
        action();
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"REFRESH_SCOPE "<<name<<" ms="<<elapsed<<" mesh="<<view->base_mesh_revision()-revision<<" tree="<<resets-before<<'\n'<<std::flush;
        if(lightweight && (view->base_mesh_revision()!=revision || resets!=before || !root.isValid() ||
            view->camera_state()!=camera_before || tree->verticalScrollBar()->value()!=scroll))
            throw std::runtime_error(std::string("Presentation action rebuilt geometry/Tree or changed camera/scroll: ")+name);
        application.processEvents();
    };
    const auto item=[&] {
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole+3)=="part-occurrence")return *i;
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    measure("tree-selection",[&]{tree->setCurrentItem(item());},false);
    const auto color_preview=[&](const char* label) {
        tree->setCurrentItem(item());
        auto* action=window.findChild<QAction*>("customBodyColorAction");
        if(!action||!action->isEnabled())throw std::runtime_error("Appearance action unavailable");
        action->trigger();application.processEvents();
        auto* dialog=window.findChild<QDialog*>("bodyColorPropertiesDialog");
        auto* color=dialog?dialog->findChild<QLineEdit*>("appearanceColor"):nullptr;
        if(!color)throw std::runtime_error("Appearance editor unavailable");
        const auto initial=color->text();
        const auto original=view->grabFramebuffer();
        const auto set=[&](const QString& value) {
            color->setText(value);
            QMetaObject::invokeMethod(color,"textEdited",Qt::DirectConnection,Q_ARG(QString,value));
        };
        for(int i=0;i<6;++i)measure((std::string(label)+"-"+std::to_string(i)).c_str(),[&]{set(i%2?"#2277CC":"#CC4422");});
        const auto colored=view->grabFramebuffer();
        if(!verify(colored!=original,"Appearance preview did not change the rendered frame"))throw std::runtime_error("No color preview");
        set(initial);application.processEvents();
        const auto restored=view->grabFramebuffer();
        if(!verify(restored==original,"Restoring appearance changed geometry or unrelated pixels"))throw std::runtime_error("Appearance restoration mismatch");
        const auto capture=qEnvironmentVariable("ZIMA_VERIFY_APPEARANCE_CAPTURE");
        if(!capture.isEmpty()) {original.save(capture+"-"+label+"-original.png");colored.save(capture+"-"+label+"-colored.png");}
        dialog->reject();application.sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    };
    color_preview("appearance-preview");

    for(int i=1;i<7;++i)measure(("filter-"+std::to_string(i)).c_str(),[&]{filter->setCurrentIndex(i);});
    measure("filter-all",[&]{filter->setCurrentIndex(0);});
    measure("selection-off",[&]{selection->setChecked(false);});
    measure("selection-on",[&]{selection->setChecked(true);},false);
    const auto occurrence_path=assembly::InstancePath{}.child(assembly.components.front().occurrence_id).encoded();
    view->confirm_occurrence(occurrence_path);
    const QPointF point{view->width()/2.,view->height()/2.};
    QMouseEvent event(QEvent::MouseButtonDblClick,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    measure("show-assembly-dimensions",[&]{QApplication::sendEvent(view,&event);});
    const std::string key="placement-reference:"+first.occurrence_id+":0";
    const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.semantic_key==key;});
    if(!verify(dimension!=view->mesh().dimensions.end(),"Assembly dimension is missing"))return 1;
    const viewer::ViewerCandidate annotation{viewer::CandidateKind::Dimension,0,
        static_cast<std::size_t>(dimension-view->mesh().dimensions.begin()),assembly.document_id,key,{}};
    const auto label=view->candidate_dimension_label_position(annotation);
    const auto dimension_offered=[&] {
        if(!label)return false;
        return std::ranges::any_of(view->selection_candidates_at(*label),[&](const auto& c){return c.kind==viewer::CandidateKind::Dimension&&c.semantic_key==key;});
    };
    if(!verify(dimension_offered(),"Shown Assembly dimension is not selectable"))return 1;
    measure("finish-assembly-dimensions",[&]{if(!window.finish_parameter_dimensions())throw std::runtime_error("No Assembly dimensions to finish");});
    if(!verify(!dimension_offered(),"Finished Assembly dimension remains selectable"))return 1;
    if(!verify(!view->confirmed_candidate()&&tree->selectedItems().empty(),"Finishing dimensions left a stale selection"))return 1;
    if(!verify(window.activate_occurrence_for_test(occurrence_path),"Cannot activate Part occurrence"))return 1;
    for(int i=1;i<7;++i)measure(("active-part-filter-"+std::to_string(i)).c_str(),[&]{filter->setCurrentIndex(i);});
    measure("active-part-filter-all",[&]{filter->setCurrentIndex(0);});
    const auto predicate=view->candidate_filter();
    viewer::ViewerCandidate own{viewer::CandidateKind::Container,0,0,part.history.front().id,{},occurrence_path};
    auto other=own;other.instance_path=assembly::InstancePath{}.child(assembly.components[1].occurrence_id).encoded();
    if(!verify(predicate&&predicate(own)&&!predicate(other),"Filter changes lost the exact active occurrence restriction"))return 1;
    const auto source_height=[&] {
        const auto& mesh=view->mesh();double high=-1e100;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i)
            if(mesh.triangle_references[i].instance_path==other.instance_path)
                for(int j=0;j<3;++j)high=std::max(high,mesh.vertices[mesh.triangles[i*3+j]].z);
        return high;
    };
    const commands::Json resize{{"command","extrusion.set"},{"arguments",{
        {"container",part.history.front().id},{"length_forward_mm",15}}}};
    if(!verify(window.execute_console_command(QString::fromStdString(resize.dump())).ok&&std::abs(source_height()-15)<1e-7,
        "Source edit was not immediately displayed in repeated occurrences"))return 1;
    window.deactivate_active_occurrence_for_test();
    const auto visible=[&] {return std::ranges::any_of(view->mesh().triangle_references,[&](const auto& ref){return ref.instance_path==occurrence_path;});};
    for(int i=0;i<6;++i) {
        const bool shown=i%2!=0;
        const commands::Json toggle{{"command","component.set"},{"arguments",{{"instance_path",occurrence_path},{"visible",shown}}}};
        measure(("visibility-"+std::to_string(i)).c_str(),[&] {
            if(!window.execute_console_command(QString::fromStdString(toggle.dump())).ok)
                throw std::runtime_error("Visibility command failed");
        },false);
        if(!verify(visible()==shown,"Visibility did not update occurrence geometry"))return 1;
        const auto capture=qEnvironmentVariable("ZIMA_VERIFY_VISIBILITY_CAPTURE");
        if(!capture.isEmpty())view->grabFramebuffer().save(capture+"-"+QString::number(i)+".png");
    }
    const commands::Json hide{{"command","component.set"},{"arguments",{{"instance_path",occurrence_path},{"visible",false}}}};
    if(!verify(window.execute_console_command(QString::fromStdString(hide.dump())).ok&&!visible(),"Hide did not rebuild scene"))return 1;
    window.findChild<QAction*>("undoAction")->trigger();application.processEvents();
    if(!verify(visible(),"Undo did not restore the occurrence"))return 1;
    window.findChild<QAction*>("redoAction")->trigger();application.processEvents();
    if(!verify(!visible(),"Redo did not hide the occurrence"))return 1;
    window.findChild<QAction*>("undoAction")->trigger();application.processEvents();
    window.findChild<QAction*>("regenerateDocumentAction")->trigger();application.processEvents();
    if(!verify(std::abs(source_height()-15)<1e-7,"Regenerate lost the unsaved source Part edit"))return 1;
    color_preview("appearance-after-source-edit");
    std::cout<<"Refresh scope verification passed\n";
    return 0;
}
