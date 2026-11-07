int verify_profile_edit_visibility(QApplication& application,
    zima::app::AssemblyWorkspaceWindow& window) {
    using namespace zima::document;
    try {
        const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_PROFILE_EDIT_VISIBILITY_FILE");
        if(fixture.isEmpty())throw std::runtime_error("Missing profile edit visibility fixture");
        std::vector<zima::kernel::BodyResult> calculated;
        const auto document=PartDocument::load(
            std::filesystem::path(fixture.toStdWString()),&calculated);
        const bool sketch_case=qEnvironmentVariableIsSet(
            "ZIMA_VERIFY_SKETCH_EDIT_VISIBILITY");
        auto found=document.history.end();
        if(sketch_case) {
            for(auto candidate=document.history.begin();candidate!=document.history.end();++candidate)
                if(candidate->feature_kind==FeatureKind::Sketch)found=candidate;
        } else found=std::ranges::find(document.history,
            std::string("Tažení 03"),&HistoryContainer::name);
        if(found==document.history.end())throw std::runtime_error(
            sketch_case?"Sketch fixture is missing":"Tažení 03 is missing");
        const auto* body=document.body_owner_for_object(found->id);
        if(!body)throw std::runtime_error("Visibility target has no owning Body");
        const auto flush=[&]{application.processEvents();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            application.processEvents();};
        window.resize(1400,900);window.show();flush();
        if(!window.open_document_path(fixture))throw std::runtime_error("Cannot open visibility fixture");
        flush();
        if(!activate_test_body(application,window,body->scope.id))
            throw std::runtime_error("Cannot activate visibility target Body");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<zima::viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>("modelWorkspace"));
        if(!tree||!view)throw std::runtime_error("Missing visibility test UI");
        QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==found->id&&
                (*i)->data(0,Qt::UserRole+3).toString()=="part-container") {row=*i;break;}
        if(!row)throw std::runtime_error("Cannot find visibility target tree row");
        const auto before_vertices=view->mesh().vertices.size();
        window.show_tree_item_properties(row);flush();
        QDialog* dialog{};
        for(auto* child:window.findChildren<QDialog*>())
            if(child->isVisible()&&
                ((sketch_case&&dynamic_cast<zima::app::SketchPropertiesDialog*>(child))||
                 (!sketch_case&&dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child)))) {
                dialog=child;break;
            }
        if(!dialog||!dialog->isVisible())throw std::runtime_error(
            "Visibility target Properties did not open");
        const auto& rollback=view->mesh();
        const auto capture=view->grabFramebuffer();
        if(const auto path=qEnvironmentVariable("ZIMA_PROFILE_EDIT_CAPTURE");!path.isEmpty())
            capture.save(path);
        std::cout<<"Profile edit visibility before_vertices="<<before_vertices
            <<" rollback_vertices="<<rollback.vertices.size()
            <<" rollback_triangles="<<rollback.triangles.size()/3
            <<" rollback_edges="<<rollback.edges.size()
            <<" transient_edges="<<view->property("transientEdgeCount").toULongLong()
            <<'\n';
        if(rollback.vertices.empty()||rollback.triangles.empty())
            throw std::runtime_error("Opening Properties hid the rollback model");
        if(capture.isNull())throw std::runtime_error("Editing Tažení 03 has no rendered View");
        if(sketch_case) {
            auto* edit=dialog->findChild<QPushButton*>("sketchOpenButton");
            if(!edit||!edit->isEnabled())throw std::runtime_error(
                "Sketch Properties has no enabled editor entry");
            edit->click();flush();
            if(view->mesh().vertices.empty()||view->mesh().triangles.empty())
                throw std::runtime_error("Entering Sketcher hid its input model");
            std::cout<<"Sketch edit visibility sketcher_vertices="
                <<view->mesh().vertices.size()<<" sketcher_triangles="
                <<view->mesh().triangles.size()/3<<'\n';
        } else {dialog->reject();flush();}
        std::cout<<"Profile edit visibility passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
