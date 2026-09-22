int verify_symbol_ui(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const auto prepare=[](auto& text){app::rebuild_sketch_text_contours(text,true);};
        const auto target=directory/"symbol-ui.tblz";
        drawing::save_template_sketch(drawing::create_template_sketch(true,"Symbols"),target);
        check(window.open_document_path(QString::fromStdString(target.string())),"Cannot open symbol fixture");window.show();flush();
        auto* insert=window.findChild<QAction*>("insertSymbolAction");auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(insert&&insert->isEnabled()&&tree&&view&&save,"Symbol command unavailable");
        QTimer chooser;QObject::connect(&chooser,&QTimer::timeout,[&]{if(auto* file=window.findChild<QFileDialog*>()) {
            file->findChild<QLineEdit*>("fileNameEdit")->setText(QString::fromStdString((std::filesystem::current_path()/"config/symbols/drawing-conventions/ZE-PROJECTION-METHOD.symz").string()));
            QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);chooser.stop();
        }});
        for(bool accept:{false,true}) {
            chooser.start(100);insert->trigger();chooser.stop();flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");
            check(dialog,"Symbol insertion dialog missing");check((dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Symbol uses a native property dialog");
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(25);
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement1")->setValue(15);
            if(accept) {
                QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(view->mapToGlobal(QPoint(20,20))),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
                QApplication::sendEvent(view,&confirm);
            }else dialog->reject();flush();save->trigger();flush();
            check(drawing::load_template_sketch(target,prepare).symbols.size()==(accept?1:0),"Symbol OK/Cancel did not preserve ownership");
        }
        const auto original=drawing::load_template_sketch(target,prepare).symbols.front();
        const auto select=[&](){QTreeWidgetItemIterator it(tree);while(*it){auto* item=*it++;if(item->data(0,Qt::UserRole+3)=="sketch-symbol"){tree->setCurrentItem(item);return item;}}return static_cast<QTreeWidgetItem*>(nullptr);};
        auto* item=select();check(item,"Symbol Tree row missing");flush();
        check(view->confirmed_candidate()&&view->confirmed_candidate()->kind==viewer::CandidateKind::Symbol,"Tree did not select symbol in View");
        for(const QString language:{"cs","en","de","fr","ru"}) {
            auto settings=app::ApplicationSettings::load();settings.language=language;
            QSettings config(QString::fromStdString((directory/"symbol-language.ini").string()),QSettings::IniFormat);config.setValue("Application/Language",language);config.sync();
            QTemporaryDir translated;QFile::copy(config.fileName(),translated.filePath("config.ini"));settings=app::ApplicationSettings::load(translated.path());
            app::apply_application_translations(application,settings);
            window.show_tree_item_properties(select());flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");check(dialog,"Cannot edit symbol");
            check(dialog->findChild<QCheckBox*>("symbolCadVariant")->text()==settings.qt_translations.value("Podle nastavení výkresu"),"Symbol translation differs");
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(99);dialog->reject();flush();save->trigger();flush();
            check(drawing::load_template_sketch(target,prepare).symbols.front()==original,"Symbol edit Cancel changed document");
        }
        app::apply_application_translations(application,app::ApplicationSettings::load());
        window.show_tree_item_properties(select());flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");
        dialog->findChild<QCheckBox*>("symbolCadVariant")->setChecked(false);auto* variant=dialog->findChild<QComboBox*>("symbolVariant");variant->setCurrentIndex(variant->findData("third_angle"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
        const auto edited=drawing::load_template_sketch(target,prepare).symbols.front();check(edited.variant=="third_angle"&&!edited.use_cad_variant,"Symbol variant did not persist");
        view->clear_selection();view->fit_all();flush();
        const auto rendered=view->grabFramebuffer();
        std::size_t white_pixels=0;
        for(int y=0;y<rendered.height();++y)for(int x=0;x<rendered.width();++x) {
            const auto color=rendered.pixelColor(x,y);
            if(color.red()>230&&color.green()>230&&color.blue()>230)++white_pixels;
        }
        check(white_pixels>100,"Symbol geometry is stored but not painted in Sketcher");
        window.grab().save(QString::fromStdString((directory/"symbol-editor.png").string()));
        std::cout<<"Symbol insertion, edit, cancellation, middle-button confirmation, Tree/View selection and five UI languages passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
