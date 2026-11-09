static int verify_sheet_profile_ui(QApplication& application,
        const std::filesystem::path& parent) {
    using namespace zima;
    const auto require=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    const auto flush=[&] {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
    };
    try {
        QTemporaryDir isolated(QString::fromStdString(
            (parent/"sheet-profile-ui-XXXXXX").string()));
        require(isolated.isValid(),"Cannot isolate Sheet Profile UI test");
        const auto directory=std::filesystem::path(
            isolated.path().toStdWString());
        auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Body");
        part.set_body_history(std::move(graph));
        document::set_sheet_metal_defaults(part,{2.5,.42});
        const auto path=directory/"sheet-profile-ui.prtz";
        part.save(path,{});

        app::AssemblyWorkspaceWindow window(isolated.path());
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1400,1000);window.show();
        require(window.open_document_path(
            QString::fromStdWString(path.wstring())),
            "Sheet Profile UI fixture did not open");
        flush();
        require(activate_test_body(application,window,body),
            "Sheet Profile Body could not activate");
        auto* sheet_mode=window.findChild<QAction*>("applicationModeAction2");
        require(sheet_mode,"Sheet Metal mode is missing");
        sheet_mode->trigger();flush();
        auto* action=window.findChild<QAction*>("sheetProfileAction");
        require(action&&action->isEnabled()&&!action->icon().isNull(),
            "Sheet Profile action is unavailable or has no icon");
        action->trigger();flush();
        auto* dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("sheetProfilePropertiesDialog"));
        require(dialog&&dialog->isVisible()&&
            dialog->windowType()==Qt::SubWindow,
            "Sheet Profile did not open in the shared internal Properties window");
        auto* custom=dialog->findChild<QCheckBox*>(
            "sheetProfileThicknessOverride");
        auto* thickness=dialog->findChild<QDoubleSpinBox*>(
            "sheetProfileThickness");
        require(custom&&!custom->isChecked()&&thickness&&
            !thickness->isEnabled()&&std::abs(thickness->value()-2.5)<1e-9,
            "New Sheet Profile does not inherit the Part sheet thickness");
        require(!dialog->findChild<QComboBox*>("sheetProfileMaterialSide")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileMaterialSideSwitch")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileOtherEndpoint")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileManualPlacement")&&
            dialog->findChild<QPushButton*>("sketchOpenButton"),
            "Sheet Profile still exposes manual attachment controls or lost its Sketch control");
        require(!dialog->findChild<QWidget*>("sheetProfileWidth"),
            "Sheet Profile exposes a duplicate width control");
        auto [driver,placement]=dialog->pending_value();
        static_cast<void>(placement);
        static_cast<void>(driver.add_segment(0,0,20,0));
        static_cast<void>(driver.add_arc(20,5,20,0,25,5));
        static_cast<void>(driver.add_segment(25,5,25,25));
        dialog->set_pending_sketch(std::move(driver));flush();
        auto* view=dynamic_cast<viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>());
        require(view&&!view->transient_edges().empty(),
            "Sheet Profile Sketch return produced no cyan feature wire");
        dialog->findChild<QDialogButtonBox*>()->button(
            QDialogButtonBox::Ok)->click();flush();
        require(!window.findChild<QDialog*>("sheetProfilePropertiesDialog"),
            "Sheet Profile OK did not close Properties");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();

        auto saved=document::PartDocument::load(path);
        const auto parent_feature=std::ranges::find(saved.history,
            document::FeatureKind::SheetProfile,
            &document::HistoryContainer::feature_kind);
        require(parent_feature!=saved.history.end()&&
            !parent_feature->sheet_profile.thickness_override&&
            std::abs(parent_feature->sheet_profile.thickness-2.5)<1e-9,
            "Sheet Profile creation lost inherited thickness");
        std::vector<std::string> children;
        for(const auto& feature:saved.history)
            if(feature.sheet_profile_binding.parent_id==parent_feature->id)
                children.push_back(feature.id);
        require(children.size()==3,
            "Sheet Profile UI did not create Flat/Bend/Flat native children");
        kernel::OcctKernel kernel;
        const auto calculated=kernel.evaluate_history(saved.kernel_operations());
        require(!calculated.empty()&&
            calculated.back().calculation_errors.empty(),
            "Sheet Profile GUI OK did not calculate its generated features");
        const auto calculated_body=calculated.back().body_outputs.find(body);
        require(calculated_body!=calculated.back().body_outputs.end()&&
            !calculated_body->second->kernel_shape.empty()&&
            calculated_body->second->volume>0,
            "Sheet Profile GUI OK produced no visible solid Body");

        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        QTreeWidgetItem* profile_row{};
        std::map<std::string,QTreeWidgetItem*> child_rows;
        for(QTreeWidgetItemIterator row(tree);*row;++row) {
            const auto id=(*row)->data(0,Qt::UserRole).toString().toStdString();
            if(id==parent_feature->id)profile_row=*row;
            if(std::ranges::find(children,id)!=children.end())child_rows[id]=*row;
        }
        require(profile_row&&child_rows.size()==children.size()&&
            std::ranges::all_of(children,[&](const auto& id) {
                return child_rows.at(id)->parent()==profile_row;
            }),"Generated Sheet Profile children are not grouped below Profile");
        window.show_tree_item_properties(profile_row);flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("sheetProfilePropertiesDialog"));
        require(dialog&&dialog->isVisible(),
            "Saved Sheet Profile Properties did not reopen");
        dialog->reject();flush();
        profile_row=nullptr;
        for(QTreeWidgetItemIterator row(tree);*row;++row)
            if((*row)->data(0,Qt::UserRole).toString().toStdString()==
                    parent_feature->id) {
                profile_row=*row;break;
            }
        require(profile_row,"Sheet Profile row disappeared after Cancel");

        bool exploded{};
        QTimer::singleShot(0,&window,[&] {
            auto* menu=window.findChild<QMenu*>("partHistoryMenu");
            auto* explode=menu?menu->findChild<QAction*>(
                "explodeSheetProfileAction"):nullptr;
            if(!explode||!explode->isEnabled())return;
            exploded=true;menu->setActiveAction(explode);
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(menu,&enter);
        });
        tree->customContextMenuRequested(
            tree->visualItemRect(profile_row).center());flush();
        require(exploded,"Explode Profile context action is unavailable");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto decomposed=document::PartDocument::load(path);
        require(!decomposed.find_container(parent_feature->id)&&
            std::ranges::all_of(children,[&](const auto& id) {
                const auto* child=decomposed.find_container(id);
                return child&&child->sheet_profile_binding.parent_id.empty();
            }),"Explode Profile did not retain independent native children");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto restored=document::PartDocument::load(path);
        require(restored.find_container(parent_feature->id)&&
            std::ranges::all_of(children,[&](const auto& id) {
                const auto* child=restored.find_container(id);
                return child&&child->sheet_profile_binding.parent_id==
                    parent_feature->id;
            }),"Undo did not restore the complete Sheet Profile group");
        std::cout<<"Sheet Profile dialog, Tree, Explode and Undo passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
