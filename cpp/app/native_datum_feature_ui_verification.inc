static int verify_native_datum_feature_ui(QApplication& application,
        const std::filesystem::path& parent_directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    struct ModalTrap:QObject {
        QString error;
        bool eventFilter(QObject* object,QEvent* event) override {
            if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object)) {
                box->setAttribute(Qt::WA_DontShowOnScreen);error=box->text();
                QTimer::singleShot(0,box,[box]{box->accept();});
            }
            return false;
        }
    } trap;
    application.installEventFilter(&trap);
    try {
        QTemporaryDir isolated(QString::fromStdString((parent_directory/"native-datum-feature-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create isolated native datum workspace");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_NATIVE_DATUM_FEATURE_SOURCE").toStdString());
        std::vector<kernel::BodyResult> cached;const auto authored=document::PartDocument::load(source,&cached);
        const auto feature=std::ranges::find(authored.history,std::string("Tažení 04"),&document::HistoryContainer::name);
        check(feature!=authored.history.end(),"Native datum fixture has no Tažení 04");
        const auto profile=std::ranges::find(authored.sketches,feature->id,&sketcher::Sketch::owner_container_id);
        check(profile!=authored.sketches.end()&&profile->circles.size()==2,"Native datum fixture has no annular profile");
        const auto diameter=std::ranges::find(profile->dimensions,sketcher::DimensionKind::Diameter,&sketcher::SketchDimension::kind);
        check(diameter!=profile->dimensions.end(),"Native datum fixture has no driving diameter");
        const auto file=directory/"02-feature04-ui.prtz";authored.save(file,cached);
        app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1366,900);window.show();flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
        };
        check(window.open_document_path(QString::fromStdString(file.string())),"Native datum fixture cannot open");flush();run("regenerate");run("save");
        const auto baseline=document::PartDocument::load(file);
        const auto* body=baseline.body_owner_for_object(feature->id);check(body&&activate_test_body(application,window,body->scope.id),"Native datum Body cannot activate");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        check(view&&!view->mesh().triangles.empty(),"Recovered native datum model has no display");
        const auto open_properties=[&] {
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature->id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row,"Tažení 04 has no Feature Tree row");window.show_tree_item_properties(row);flush();
            auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog&&dialog->pending_value().placement.reference_valid,"Tažení 04 Properties lost its native source point");return dialog;
        };
        const auto revision=[&]{return run("documents").front().at("revision").get<std::uint64_t>();};
        const auto before=revision();auto* unchanged=open_properties();
        unchanged->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(trap.error.isEmpty()&&revision()==before,"Unchanged Tažení 04 OK failed or created a model revision");
        for(double value:{126.,118.,120.}) {
            const auto old_revision=revision();auto* dialog=open_properties();
            auto* open=dialog->findChild<QPushButton*>("featureSketchButton");check(open&&open->isVisible(),"Tažení 04 has no Sketch button");open->click();flush();
            const auto* draft=window.active_sketch_for_test();check(draft&&draft->id==profile->id,"Tažení 04 did not enter its actual Sketch draft");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=profile->id;candidate.semantic_key="dimension:"+diameter->id;
            const auto shown=std::ranges::find(view->mesh().dimensions,candidate.semantic_key,[](const auto& d){return d.reference.semantic_key;});
            check(shown!=view->mesh().dimensions.end(),"Tažení 04 diameter is absent from View");candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
            window.edit_dimension_inline(candidate);flush();auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(editor&&editor->isVisible(),"Tažení 04 diameter editor missing");editor->setText(QString::number(value,'g',17));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            draft=window.active_sketch_for_test();
            const auto outer=std::ranges::find(draft->circles,diameter->geometry_id,&sketcher::SketchCircle::id);
            check(outer!=draft->circles.end()&&std::abs(2*outer->radius-value)<1e-7,"Tažení 04 diameter did not change its actual circle");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog&&dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Tažení 04 cannot confirm its edited profile");
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(!trap.error.isEmpty())throw std::runtime_error(trap.error.toStdString());
            check(revision()==old_revision+1&&!view->mesh().triangles.empty(),"Tažení 04 edit did not commit once with a visible Body");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> restored;const auto saved=document::PartDocument::load(file,&restored);
            check(saved.history.size()==authored.history.size()&&saved.find_container(feature->id)->placement.reference_valid&&
                !restored.empty()&&restored.back().calculation_errors.empty()&&!restored.back().mesh.triangles.empty(),"Tažení 04 edit failed native save/reopen");
            for(const auto& sketch:baseline.sketches)if(sketch.id!=profile->id)
                check(std::ranges::find(saved.sketches,sketch.id,&sketcher::Sketch::id)->serialized()==sketch.serialized(),"Tažení 04 editing changed an earlier source Sketch");
        }
        app::AssemblyWorkspaceWindow reopened(isolated.path());reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.resize(1200,900);reopened.show();
        check(reopened.open_document_path(QString::fromStdString(file.string())),"Tažení 04 edited file cannot open in a fresh window");flush();
        check(!dynamic_cast<viewer::MeshView*>(reopened.findChild<QOpenGLWidget*>())->mesh().triangles.empty(),"Tažení 04 reopened without model display");
        std::cout<<"Tažení 04 GUI: native point through Plane, unchanged OK, three actual diameter edits, one revision per edit, source immutability and fresh native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Native datum Feature GUI: "<<e.what()<<'\n';return 1;}
}
