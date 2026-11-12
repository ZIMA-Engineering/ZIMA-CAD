int verify_planar_surface_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* message) {
            if(!value)throw std::runtime_error(message);
        };
        const auto flush=[&] {
            application.processEvents();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            application.processEvents();
        };
        window.resize(1280,950);window.show();flush();
        auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Planar Surface GUI");
        part.set_body_history(graph);
        const auto path=directory/"planar-surface-ui.prtz";
        part.save(path);
        check(window.open_document_path(QString::fromStdString(path.string())),
            "Cannot open planar Surface fixture");
        flush();
        check(window.execute_console_command(QString::fromStdString(
            commands::Json{{"command","body.activate"},
                {"arguments",{{"body",body}}}}.dump())).ok,
            "Cannot activate planar Surface Body");
        flush();
        auto* surface_mode=window.findChild<QAction*>("applicationModeAction3");
        check(surface_mode&&surface_mode->isEnabled(),
            "Surface application is unavailable");
        surface_mode->trigger();flush();
        auto* planar=window.findChild<QAction*>("planarSurfaceAction");
        auto* general=window.findChild<QAction*>("generalSurfaceAction");
        auto* toolbar=window.findChild<QToolBar*>("toolsToolbar");
        check(planar&&general&&toolbar&&planar->isEnabled(),
            "Planar Surface command is unavailable");
        const auto actions=toolbar->actions();
        check(actions.indexOf(planar)>=0&&
                actions.indexOf(planar)<actions.indexOf(general),
            "Planar Surface is not the first Surface modeling command");
        const auto document_state=window.execute_console_command("documents").data;
        planar->trigger();flush();
        auto* dialog=dynamic_cast<SketchPropertiesDialog*>(
            window.findChild<QWidget*>("planarSurfacePropertiesDialog"));
        check(dialog&&dialog->windowType()==Qt::SubWindow,
            "Planar Surface did not open the shared internal Sketch properties");
        auto* name=dialog->findChild<QLineEdit*>("sketchName");
        auto* plane=dialog->findChild<QComboBox*>("sketchPlane");
        auto* offset=dialog->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
        auto* sketch=dialog->findChild<QPushButton*>("sketchOpenButton");
        check(name&&plane&&offset&&offset->isVisible()&&sketch&&sketch->isVisible(),
            "Planar Surface is missing Name, work plane, offset or owned Sketch");
        check(!dialog->findChild<QWidget*>("featureDirection")&&
                !dialog->findChild<QWidget*>("featureResult")&&
                !dialog->findChild<QWidget*>("flatDirection"),
            "Planar Surface exposes extrusion, result or sheet direction controls");
        offset->setValue(12.5);flush();
        check(std::abs(offset->value()-12.5)<1e-8,
            "Planar Surface plane offset is not editable");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(!window.findChild<QWidget*>("planarSurfacePropertiesDialog"),
            "Planar Surface Cancel did not close the properties window");
        check(window.execute_console_command("documents").data==document_state,
            "Planar Surface Cancel changed the document");
        general->trigger();flush();
        auto* general_dialog=window.findChild<QWidget*>("generalSurfaceDialog");
        check(general_dialog,
            "Adding Planar Surface changed the existing General Surface command");
        auto* buttons=general_dialog->findChild<QDialogButtonBox*>();
        check(buttons,"General Surface actions are missing");
        buttons->button(QDialogButtonBox::Cancel)->click();flush();
        std::cout<<"Planar Surface command order, shared dialog fields, offset, Cancel and General Surface independence passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
