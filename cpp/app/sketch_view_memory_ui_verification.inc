static int verify_sketch_view_memory_ui(QApplication& application,
        const std::filesystem::path& parent_directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
    const auto run=[&](app::AssemblyWorkspaceWindow& window,const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(
            commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        flush();return result.data;
    };
    const auto enter=[&](app::AssemblyWorkspaceWindow& window,const std::string& id) {
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&
            ((*i)->data(0,Qt::UserRole+3)=="part-sketch"||(*i)->data(0,Qt::UserRole+3)=="assembly-sketch")){row=*i;break;}
        check(row,"Saved Sketch has no editable Tree row");window.show_tree_item_properties(row);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");
        check(open&&open->isVisible(),"Sketch Properties has no Open action");open->click();settle();
    };
    const auto finish=[&](app::AssemblyWorkspaceWindow& window) {
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())
            if(auto* box=dialog->findChild<QDialogButtonBox*>();box&&box->button(QDialogButtonBox::Ok)) {
                box->button(QDialogButtonBox::Ok)->click();flush();break;
            }
    };
    try {
        QTemporaryDir isolated(QString::fromStdString((parent_directory/"sketch-view-memory-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create isolated camera workspace");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        QSettings preferences(isolated.filePath("config.ini"),QSettings::IniFormat);
        preferences.setValue("Application/Language","en");
        for(const auto* key:{"Uppercase","RemoveDiacritics","ReplaceSpaces"})preferences.setValue(QString("DocumentNames/")+key,false);
        preferences.sync();
        for(const QString unit:{"mm","in"})for(const QString kind:{"PART","SKELETON","ASSEMBLY"}) {
            const bool is_assembly=kind=="ASSEMBLY";
            QTemporaryDir settings(QString::fromStdString((directory/"start-context-XXXXXX").string()));
            check(settings.isValid(),"Cannot create isolated template settings");
            QSettings config(settings.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language","en");config.setValue("Units/Length",unit);
            for(const auto* key:{"Uppercase","RemoveDiacritics","ReplaceSpaces"})config.setValue(QString("DocumentNames/")+key,false);
            config.setValue("Paths/Templates",QDir::current().absoluteFilePath("config/templates"));
            config.setValue(is_assembly?"Templates/Assembly":"Templates/Part","START_"+kind+"_"+unit+(is_assembly?".asmz":".prtz"));config.sync();
            app::AssemblyWorkspaceWindow window(settings.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            run(window,"new",{{"type",is_assembly?"assembly":"part"},{"name","template-context"}});
            auto* normal=window.findChild<QAction*>(is_assembly?"insertComponentAction":"featurePrototypeAction");
            check(normal&&normal->isEnabled(),"Regenerated template has no ordinary command context");
            run(window,"save");
            const auto file=std::filesystem::u8path(settings.filePath(is_assembly?"template-context.asmz":"template-context.prtz").toStdString());
            if(!is_assembly) {
                const auto part=document::PartDocument::load(file);
                check(part.body_history.bodies().size()==1&&!part.body_history.active_body_id().empty(),"Regenerated Part/Skeleton template has no active editable Body");
            } else check(assembly::AssemblyDocument::load(file).components.empty(),"Regenerated Assembly template has unexpected components");
        }
        unsigned cases{};
        for(bool in_assembly:{false,true})for(bool back:{false,true})for(int turn=0;turn<4;++turn) {
            std::array<std::string,2> sketches;
            std::array<std::array<float,8>,2> cameras;
            const auto file=directory/("sketch-view-memory-"+std::to_string(cases)+(in_assembly?".asmz":".prtz"));
            {
                app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
                run(window,"new",{{"type",in_assembly?"assembly":"part"},{"name",file.stem().string()}});
                auto* normal=window.findChild<QAction*>(in_assembly?"insertComponentAction":"featurePrototypeAction");
                check(normal&&normal->isEnabled(),"Start template did not activate its ordinary command context");
                for(int i=0;i<2;++i) {
                    sketches[i]=run(window,"sketch.create",{{"name",i?"Second":"First"},{"plane",i?"XZ":"XY"}}).at("sketch");
                    run(window,"sketch.segment.create",{{"sketch",sketches[i]},{"first",{12,7}},{"second",{35,24}}});
                }
                run(window,"save");
                const auto before=in_assembly?assembly::AssemblyDocument::load(file).serialized():document::PartDocument::load(file).serialized();
                for(int i=0;i<2;++i) {
                    enter(window,sketches[i]);
                    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Sketch View is missing");
                    const auto documents=run(window,"documents");const auto base=view->base_mesh_revision();
                    if(i?!back:back)window.findChild<QAction*>("sketchFlipViewAction")->trigger();
                    for(int q=0;q<(i?(turn+1)%4:turn);++q)window.findChild<QAction*>("sketchRotateViewAction")->trigger();
                    settle();cameras[i]=view->camera_state();
                    check(run(window,"documents").front().at("revision")==documents.front().at("revision")&&view->base_mesh_revision()==base,
                        "Sketch camera rotation recalculated/published geometry or created modeling Undo");
                    finish(window);
                }
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                auto after=in_assembly?assembly::AssemblyDocument::load(file).serialized():document::PartDocument::load(file).serialized();
                auto expected=before;expected.erase("sketch_views");after.erase("sketch_views");
                check(after==expected,"Saving a Sketch camera changed geometry, identities or document properties");
            }
            {
                app::AssemblyWorkspaceWindow reopened(QString::fromStdString(directory.string()));reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.resize(1200,900);reopened.show();
                check(reopened.open_document_path(QString::fromStdString(file.string())),"Saved camera document cannot reopen");flush();
                for(int i=0;i<2;++i) {
                    enter(reopened,sketches[i]);
                    const auto camera=dynamic_cast<viewer::MeshView*>(reopened.findChild<QOpenGLWidget*>())->camera_state();
                    double dot{};for(int q=0;q<4;++q)dot+=camera[q]*cameras[i][q];
                    check(std::abs(std::abs(dot)-1)<1e-5,"Sketch lost its relative camera after application/document reopen");
                    finish(reopened);
                }
            }
            ++cases;
        }
        std::cout<<"Sketch view memory: "<<cases<<" Part/Assembly, both sides and all quarter turns, two Sketches and fresh-window native reopen passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Sketch view memory: "<<e.what()<<'\n';return 1;}
}
