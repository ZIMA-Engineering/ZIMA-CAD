int verify_form_point_end_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_FORM_POINT_END_DOCUMENT").toStdString());
        const auto path=directory/"form-point-end.prtz";std::vector<kernel::BodyResult> cached;auto original=document::PartDocument::load(source,&cached);original.save(path,cached);const auto before=document::PartDocument::load(path).serialized();
        const auto shape=std::ranges::find_if(original.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Feature&&f.feature.result_type==document::ProfileResultType::Surface;});
        check(shape!=original.history.end(),"Point end fixture has no surface Feature");
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        check(window.open_document_path(QString::fromStdString(path.string())),"Point end fixture open failed");flush();
        check(activate_test_body(application,window,original.body_history.owner(shape->id)->scope.id),"Point end Body activation failed");
        const auto open=[&]()->app::PrimitivePropertiesDialog* {
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==shape->id){row=*i;break;}
            check(row,"Point end Feature row missing");window.show_tree_item_properties(row);flush();
            for(auto* widget:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);dialog&&dialog->isVisible())return dialog;
            throw std::runtime_error("Point end Properties missing");
        };
        auto* dialog=open();auto* ending=dialog->findChild<QComboBox*>("featureSideEnd0");check(ending,"Point end extent control missing");
        ending->setCurrentIndex(0);flush();ending->setCurrentIndex(1);flush();
        check(dialog->allows_point_end_target(),"Up to did not activate point entry");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Point end View missing");
        const auto contract=view->selection_contract();check(std::ranges::find(contract,viewer::CandidateKind::SketchPoint)!=contract.end(),"Up to did not arm Sketch point picking");
        QCursor::setPos(window.mapToGlobal(QPoint(5,5)));QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
        const auto& cut=original.sketches.front();const auto stop=std::ranges::find_if(cut.points,[](const auto& p){return p.y>0;});check(stop!=cut.points.end(),"Point end fixture lacks positive stop point");
        std::optional<QPointF> hit;
        for(int y=8;y<view->height()-8&&!hit;y+=2)for(int x=8;x<view->width()-8;x+=2){const auto c=view->selection_candidates_at({double(x),double(y)});if(!c.empty()&&c.front().owner_id==cut.id&&c.front().semantic_key=="point:"+stop->id){hit=QPointF(x,y);break;}}
        check(hit.has_value(),"Earlier Sketch point has no Up to mouse candidate");
        const auto mouse=[&](QEvent::Type type,Qt::MouseButton button){QMouseEvent e(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),button,type==QEvent::MouseButtonRelease?Qt::NoButton:button,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
        mouse(QEvent::MouseMove,Qt::NoButton);mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
        const auto pending=dialog->pending_value();check(!pending.feature.sides[0].targets.empty()&&pending.feature.sides[0].targets.front().kind==document::EndTargetKind::Point&&pending.feature.sides[0].targets.front().reference.semantic_key=="point:"+stop->id,"Actual point click did not set extrusion target");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(document::PartDocument::load(path).serialized()==before,"Point end Cancel modified the saved document");
        dialog=open();ending=dialog->findChild<QComboBox*>("featureSideEnd0");ending->setCurrentIndex(1);flush();
        mouse(QEvent::MouseMove,Qt::NoButton);mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
        check(!dialog->pending_value().feature.sides[0].targets.empty(),"Repeated point entry failed");
        QPointer<QDialog> committed_dialog=dialog;dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!committed_dialog||!committed_dialog->isVisible(),"Point end OK did not commit");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto committed=document::PartDocument::load(path);check(committed.find_container(shape->id)->feature.sides[0].targets.front().kind==document::EndTargetKind::Point,"GUI point end did not survive native save");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).find_container(shape->id)->feature.sides[0].targets.front().reference.semantic_key=="point:"+stop->id,"GUI point end Undo/Redo lost source identity");
        std::cout<<"Point end Properties: automatic reference activation, actual earlier-Sketch point click, Cancel/OK, native save and Undo/Redo passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
