int verify_multisolid_shell_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        std::vector<kernel::BodyResult> cached;
        const auto original=document::PartDocument::load(std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_MULTISOLID_SHELL_UI_FILE").toStdWString()),&cached);
        check(!cached.empty(),"Shell GUI input cache missing");
        const auto& mesh=cached.back().mesh;double z=1e100;
        for(const auto& p:mesh.vertices)z=std::min(z,p.z);
        std::map<std::pair<std::string,std::string>,kernel::FaceReference> openings;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
            bool bottom=true;for(int j=0;j<3;++j)bottom=bottom&&std::abs(mesh.vertices.at(mesh.triangles.at(i*3+j)).z-z)<1e-6;
            const auto& face=mesh.triangle_references[i];if(bottom&&face.valid())openings.emplace(std::pair{face.owner_id,face.semantic_key},face);
        }
        check(openings.size()==4,"Expected four disconnected source openings");
        const auto path=directory/"multisolid-shell-copy.prtz";original.save(path,cached);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Shell GUI copy failed to open");flush();
        check(activate_test_body(application,window,original.body_history.bodies().front().scope.id),"Shell GUI Body unavailable");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Shell GUI View missing");
        const auto dialog=[&] {app::PrimitivePropertiesDialog* result{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(d);p&&p->isVisible())result=p;return result;};
        const auto find_row=[&](const std::string& id){auto* tree=window.findChild<QTreeWidget*>("documentTree");for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id)return *i;return static_cast<QTreeWidgetItem*>(nullptr);};
        const auto state=window.execute_console_command("documents").data;
        auto* action=window.findChild<QAction*>("shellAction");check(action&&action->isEnabled(),"Shell GUI command unavailable");action->trigger();flush();
        check(dialog(),"Shell creation dialog missing");dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==state,"Shell Cancel changed document revision");
        action->trigger();flush();check(dialog(),"Shell creation dialog missing after Cancel");
        std::vector<kernel::FaceReference> selected;
        for(const auto& [key,face]:openings)selected.push_back(face);
        // Populate the shared reference table deterministically. Mouse picking
        // of all four supplied openings is a separate, unverified GUI variant.
        dialog()->set_shell_faces(selected);flush();
        check(dialog()->pending_value().shell.removed_faces==selected,"Shell reference table lost openings");
        dialog()->findChild<QDoubleSpinBox*>("shellThickness")->setValue(3);flush();
        const auto id=dialog()->pending_value().id;
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!dialog(),"Multi-solid Shell OK did not close");
        check(window.execute_console_command("save").ok,"Shell GUI Save failed");
        std::vector<kernel::BodyResult> result;const auto committed=document::PartDocument::load(path,&result);
        check(committed.history.size()==original.history.size()+1&&committed.history.back().shell.removed_faces.size()==4,"Shell GUI OK lost history or openings");
        check(result.back().calculation_errors.empty()&&result.back().volume>0&&result.back().volume<cached.back().volume,"Shell GUI result failed");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(window.execute_console_command("save").ok,"Shell GUI Undo Save failed");
        check(document::PartDocument::load(path).history==original.history,"Shell GUI Undo changed input history");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(window.execute_console_command("save").ok,"Shell GUI Redo Save failed");
        check(document::PartDocument::load(path).history==committed.history,"Shell GUI Redo lost definition");
        auto* row=find_row(id);check(row,"Shell Tree row missing");window.show_tree_item_properties(row);flush();check(dialog(),"Shell edit dialog missing");
        dialog()->findChild<QDoubleSpinBox*>("shellThickness")->setValue(5);flush();
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(window.execute_console_command("save").ok,"Shell edit Cancel Save failed");
        check(document::PartDocument::load(path).history==committed.history,"Shell edit Cancel changed definition");
        row=find_row(id);check(row,"Shell Tree row missing after Cancel");window.show_tree_item_properties(row);flush();check(dialog(),"Shell unchanged edit missing");
        const auto unchanged=window.execute_console_command("documents").data;dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("documents").data==unchanged,"Unchanged Shell OK added transaction");
        std::cout<<"Supplied four-solid Shell GUI: shared reference table, creation OK/Cancel, native result/reopen, Undo/Redo, edit Cancel and unchanged OK passed"<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
