static int verify_vector_profiles_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& root) {
    try {
        const auto directory=root/("vector_profiles-"+QUuid::createUuid().toString(QUuid::Id128).toStdString());
        std::filesystem::create_directories(directory);
        const auto check=[](bool v,const char* text){if(!v)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::cerr<<"Vector GUI: fixture\n";
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;
        sketcher::TemplateImage image;image.id="retained-svg";image.name="profile.svg";image.format="svg";image.width=image.height=image.pixel_width=image.pixel_height=20;image.template_coordinates=false;
        image.data_base64=QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 20 20'><circle cx='10' cy='10' r='4'/></svg>").toBase64().toStdString();sketch.reference_images.push_back(image);
        auto editable_text=sketcher::Sketch::create_text();editable_text.value="OB8";editable_text.height=10;sketcher::rebuild_text_contours(editable_text);sketch.add_text(editable_text);
        part.history={owner};part.sketches={sketch};document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Vector profiles"));bodies.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(bodies);
        const auto file=directory/"vector-profiles-ui.prtz";part.save(file);check(window.open_document_path(QString::fromStdString(file.string())),"Cannot open vector profile fixture");window.showMaximized();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const QString& kind,const std::string& id=std::string{}) {for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3).toString()==kind&&(id.empty()||(*i)->data(0,Qt::UserRole).toString().toStdString()==id))return *i;return static_cast<QTreeWidgetItem*>(nullptr);};
        auto* sr=row("part-sketch",sketch.id);check(sr,"Profile Sketch row missing");window.show_tree_item_properties(sr);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Cannot edit profile Sketch");open->click();flush();
        const auto authored=[&] {check(window.active_sketch_for_test(),"Lost active profile Sketch");return window.active_sketch_for_test()->reference_images.front().modeling_geometry;};
        check(!authored(),"Existing SVG unexpectedly became a material profile");
        std::cerr<<"Vector GUI: Sketch opened\n";
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir settings_dir;QSettings ini(settings_dir.filePath("config.ini"),QSettings::IniFormat);ini.setValue("Application/Language",language);ini.sync();
            const auto settings=ApplicationSettings::load(settings_dir.path());apply_application_translations(application,settings);
            std::cerr<<"Vector GUI: language "<<language<<'\n';
            for(bool accept:{false,true}) {
                const bool before=authored();auto* ir=row("template-image",image.id);check(ir,"SVG row missing");window.show_tree_item_properties(ir);flush();
                auto* dialog=window.findChild<QDialog*>("templateImageDialog");check(dialog,"SVG Properties missing");auto* option=dialog->findChild<QCheckBox*>("templateImageModelingGeometry");
                check(option&&option->isVisible()&&option->text()==settings.qt_translations.value("Použít jako profil tělesa"),"SVG solid option missing or untranslated");
                check(option->isChecked()==before,"SVG solid option did not reopen");option->click();flush();check(authored()==before,"SVG preview committed before OK");
                if(accept)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else dialog->reject();flush();
                check(authored()==(accept?!before:before),"SVG solid option OK/Cancel failed");
                if(accept){window.findChild<QAction*>("undoAction")->trigger();flush();check(authored()==before,"SVG solid option Undo failed");window.findChild<QAction*>("redoAction")->trigger();flush();check(authored()!=before,"SVG solid option Redo failed");}
            }
        }
        const auto font_path=QString::fromLocal8Bit(qgetenv("ZIMA_VERIFY_EMBEDDED_FONT"));
        if(!font_path.isEmpty()) {
            for(bool accept:{false,true}) {
                auto* text_row=row("sketch-geometry",editable_text.id);check(text_row,"Editable text Tree row missing");tree->setCurrentItem(text_row);tree->scrollToItem(text_row);flush();
                QTimer::singleShot(0,&window,[&]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    for(auto* action:menu->actions())if(action->objectName()=="sketchGeometryPropertiesAction") {
                        menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);break;
                    }
                }});tree->customContextMenuRequested(tree->visualItemRect(text_row).center());flush();
                auto* text_dialog=window.findChild<QDialog*>("sketchTextProperties");check(text_dialog,"Text Properties missing");
                auto* load=text_dialog->findChild<QPushButton*>("sketchTextLoadFont");check(load,"Embedded font chooser missing");
                QTimer picker;QObject::connect(&picker,&QTimer::timeout,&window,[&]{if(auto* chooser=window.findChild<QFileDialog*>()) {
                    chooser->findChild<QLineEdit*>("fileNameEdit")->setText(font_path);QMetaObject::invokeMethod(chooser,"accept",Qt::QueuedConnection);picker.stop();
                }});picker.start(50);load->click();picker.stop();flush();
                check(text_dialog->findChild<QComboBox*>("sketchTextFont")->currentData()=="embedded","Selected font was not loaded");
                check(window.active_sketch_for_test()->texts.front().embedded_font.empty(),"Font preview modified the document");
                if(accept)text_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else text_dialog->reject();flush();
                check(window.active_sketch_for_test()->texts.front().embedded_font.empty()!=accept,"Embedded font OK/Cancel failed");
            }
            const auto embedded=window.active_sketch_for_test()->texts.front().embedded_font;
            window.findChild<QAction*>("undoAction")->trigger();flush();check(window.active_sketch_for_test()->texts.front().embedded_font.empty(),"Font Undo retained the replacement");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(window.active_sketch_for_test()->texts.front().embedded_font==embedded,"Font Redo lost its original bytes");
            check(window.active_sketch_for_test()->texts.size()==1&&window.active_sketch_for_test()->bsplines.empty(),"Font load exploded the editable text");
        }
        // The DXF command inserts one retained block and uses the ordinary
        // Properties/explode paths, including transient insertion Cancel.
        std::cerr<<"Vector GUI: image flags passed\n";
        const auto dxf=directory/"profile-block.dxf";auto drawing=sketcher::Sketch::create_default();static_cast<void>(drawing.add_rectangle(0,0,8,5));static_cast<void>(drawing.add_circle(15,4,2));interchange::export_dxf(dxf,drawing);
        auto* action=window.findChild<QAction*>("sketchDxfAction");check(action&&action->isEnabled()&&!action->icon().isNull(),"Sketch DXF action missing");
        for(bool accept:{false,true}) {
            QTimer picker;QObject::connect(&picker,&QTimer::timeout,&window,[&]{if(auto* chooser=window.findChild<QFileDialog*>()){chooser->findChild<QLineEdit*>("fileNameEdit")->setText(QString::fromStdString(dxf.string()));QMetaObject::invokeMethod(chooser,"accept",Qt::QueuedConnection);picker.stop();}});
            picker.start(50);action->trigger();picker.stop();flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");check(dialog,"DXF did not open block placement");
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(30);dialog->findChild<QDoubleSpinBox*>("symbolPlacement1")->setValue(10);
            if(accept)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else dialog->reject();flush();
            check(window.active_sketch_for_test()->symbols.size()==(accept?1:0),"DXF insertion OK/Cancel changed the wrong data");
        }
        auto* block=row("sketch-symbol");check(block,"DXF block Tree row missing");tree->setCurrentItem(block);flush();
        auto* explode=window.findChild<QAction*>("explodeSketchSymbolAction");check(explode&&explode->isEnabled(),"DXF explode action missing");bool invoked=false;
        QTimer::singleShot(0,&window,[&]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            invoked=menu->actions().contains(explode);menu->close();if(invoked)explode->trigger();
        }});
        tree->customContextMenuRequested(tree->visualItemRect(block).center());flush();check(invoked,"DXF Tree context did not offer explode");
        check(window.active_sketch_for_test()->symbols.empty()&&window.active_sketch_for_test()->segments.size()==4&&window.active_sketch_for_test()->circles.size()==1,"DXF did not become native Sketch geometry");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(window.active_sketch_for_test()->symbols.size()==1,"DXF explode Undo lost the block");window.findChild<QAction*>("redoAction")->trigger();flush();
        auto* finish=window.findChild<QAction*>("finishSketchAction");finish->trigger();flush();
        for(auto* d:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<SketchPropertiesDialog*>(d);properties&&properties->isVisible()){properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();break;}
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();const auto saved=document::PartDocument::load(file);
        check(saved.sketches.front().reference_images.front().data_base64==image.data_base64&&saved.sketches.front().reference_images.front().modeling_geometry,"SVG source or profile participation did not persist");
        if(!font_path.isEmpty())check(!saved.sketches.front().texts.front().embedded_font.empty(),"Saved text lost its embedded font");
        std::cout<<"SVG profile Properties: five languages, Cancel, OK, Undo/Redo and native persistence; DXF block insertion/explosion passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Vector profile GUI: "<<e.what()<<'\n';return 1;}
}
