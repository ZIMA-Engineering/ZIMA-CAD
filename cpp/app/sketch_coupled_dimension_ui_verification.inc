static int verify_sketch_coupled_dimension_ui(QApplication& application,
        const std::filesystem::path& parent_directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    struct ModalTrap:QObject {
        QString error;
        bool eventFilter(QObject* object,QEvent* event) override {
            if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object)) {
                box->setAttribute(Qt::WA_DontShowOnScreen);error=box->text();QTimer::singleShot(0,box,[box]{box->accept();});
            }
            return false;
        }
    } trap;
    application.installEventFilter(&trap);
    try {
        QTemporaryDir isolated(QString::fromStdString((parent_directory/"coupled-dimension-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create isolated coupled dimension workspace");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_COUPLED_DIMENSION_SOURCE").toStdString());
        std::vector<kernel::BodyResult> cached;const auto authored=document::PartDocument::load(source,&cached);
        const auto profile=std::ranges::find_if(authored.sketches,[](const auto& s){return s.points.size()>32||std::ranges::any_of(s.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::Radius&&std::abs(d.value-150)<1e-6;});});
        check(profile!=authored.sketches.end(),"Coupled dimension fixture has no affected profile");
        const auto* feature=authored.find_container(profile->owner_container_id);check(feature,"Coupled dimension profile has no Feature");
        std::vector<std::pair<std::string,double>> edits;
        for(const auto& d:profile->dimensions)if(d.driving&&!d.suppressed&&
            (profile->points.size()>32||(d.kind==sketcher::DimensionKind::Radius&&std::abs(d.value-150)<1e-6)))edits.emplace_back(d.id,d.value);
        const auto file=directory/"coupled-profile-ui.prtz";authored.save(file,cached);
        app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1366,900);window.show();flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
        };
        check(window.open_document_path(QString::fromStdString(file.string())),"Coupled dimension fixture cannot open");flush();run("regenerate");run("save");
        const auto baseline=document::PartDocument::load(file);
        const auto* body=baseline.body_owner_for_object(feature->id);check(body&&activate_test_body(application,window,body->scope.id),"Coupled dimension Body cannot activate");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Coupled dimension View missing");
        const auto dialog_name=feature->feature_kind==document::FeatureKind::Flat?"flatPropertiesDialog":"featurePropertiesDialog";
        const auto properties=[&]() -> QDialog* {
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&d->objectName()==dialog_name)return d;
            return nullptr;
        };
        const auto revision=[&]{return run("documents").front().at("revision").get<std::uint64_t>();};
        for(const auto& [id,initial]:edits)for(double value:{initial+5,initial-5,initial}) {
            const auto old_revision=revision();auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature->id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row,"Coupled dimension Feature Tree row missing");window.show_tree_item_properties(row);flush();
            auto* dialog=properties();
            check(dialog,"Coupled dimension Feature Properties missing");auto* open=dialog->findChild<QPushButton*>("featureSketchButton");
            if(!open)open=dialog->findChild<QPushButton*>("sketchOpenButton");
            check(open&&open->isVisible(),"Coupled dimension Feature has no Sketch button");open->click();flush();
            const auto* draft=window.active_sketch_for_test();check(draft&&draft->id==profile->id,"Coupled dimension did not enter its actual Sketch");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=profile->id;candidate.semantic_key="dimension:"+id;
            const auto shown=std::ranges::find(view->mesh().dimensions,candidate.semantic_key,[](const auto& d){return d.reference.semantic_key;});
            check(shown!=view->mesh().dimensions.end(),"Affected dimension is absent from the actual View");candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            std::vector<kernel::Vec3> bounds;for(const auto& p:draft->points)bounds.push_back(draft->world_point(p.x,p.y));
            for(const auto& d:draft->dimensions)if(d.placement)bounds.push_back(draft->world_point((*d.placement)[0],(*d.placement)[1]));
            view->fit_points(bounds);QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();
            const auto pixel=view->candidate_dimension_label_position(candidate);check(pixel.has_value(),"Affected dimension label has no View position");
            for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                const bool down=type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick;
                QMouseEvent event(type,QPointF(*pixel),QPointF(view->mapToGlobal(*pixel)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    down?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            }
            auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(editor&&editor->isVisible(),"Affected dimension inline editor is missing");editor->setText(QString::number(value,'g',17));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            draft=window.active_sketch_for_test();const auto dimension=std::ranges::find(draft->dimensions,id,&sketcher::SketchDimension::id);
            check(dimension!=draft->dimensions.end()&&std::abs(dimension->value-value)<1e-7,"Actual inline edit rejected its value");
            check_coupled_equations(*draft);
            const auto edited=draft->serialized();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(window.active_sketch_for_test()->serialized()==edited,"Coupled inline edit Undo/Redo changed the branch");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            dialog=properties();auto* buttons=dialog?dialog->findChild<QDialogButtonBox*>():nullptr;
            check(buttons&&buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Coupled Feature cannot confirm its edited profile");
            buttons->button(QDialogButtonBox::Ok)->click();flush();
            if(!trap.error.isEmpty())throw std::runtime_error(trap.error.toStdString());
            check(revision()==old_revision+1&&!view->mesh().triangles.empty(),"Coupled edit did not commit once with visible model geometry");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> restored;const auto saved=document::PartDocument::load(file,&restored);
            check(saved.history.size()==baseline.history.size()&&!restored.empty()&&restored.back().calculation_errors.empty()&&
                !restored.back().mesh.triangles.empty(),"Coupled dimension edit failed native save/reopen");
            check_coupled_equations(*std::ranges::find(saved.sketches,profile->id,&sketcher::Sketch::id));
            for(const auto& s:baseline.sketches)if(s.id!=profile->id) {
                const auto owner=std::ranges::find(baseline.history,s.owner_container_id,&document::HistoryContainer::id);
                const auto consumer=std::ranges::find(baseline.history,feature->id,&document::HistoryContainer::id);
                const auto changed=std::ranges::find(saved.sketches,s.id,&sketcher::Sketch::id);
                if(owner<consumer)check(changed->serialized()==s.serialized(),"Coupled dimension edit modified a preceding source Sketch");
                check(changed->external_references.size()==s.external_references.size(),"Coupled dimension edit replaced a dependent source list");
                for(std::size_t i=0;i<s.external_references.size();++i) {
                    const auto &a=s.external_references[i],&b=changed->external_references[i];
                    check(a.id==b.id&&a.source_document_id==b.source_document_id&&a.source_owner_id==b.source_owner_id&&
                        a.source_semantic_key==b.source_semantic_key&&a.source_instance_path==b.source_instance_path,"Dependent reference identity changed");
                }
            }
        }
        app::AssemblyWorkspaceWindow reopened(isolated.path());reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.resize(1200,900);reopened.show();
        check(reopened.open_document_path(QString::fromStdString(file.string())),"Coupled dimension file cannot open in a fresh window");flush();
        check(!dynamic_cast<viewer::MeshView*>(reopened.findChild<QOpenGLWidget*>())->mesh().triangles.empty(),"Coupled dimension reopened without displayed geometry");
        std::cout<<"Coupled dimension GUI: "<<edits.size()<<" actual dimensions, three inline edits each, independent equations, draft Undo/Redo, one revision per Feature OK, source immutability and fresh native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Coupled dimension GUI: "<<e.what()<<'\n';return 1;}
}
