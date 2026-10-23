// Uses a private native-test fixture; never opens a user's working document.
Q_NEVER_INLINE static int verify_assembly_preview_wire_ui(QApplication& application,
        AssemblyWorkspaceWindow& window) {
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const QString& text) {
            const auto result=window.execute_console_command(text);
            if(!result.ok)throw std::runtime_error(result.code+": "+result.message);
            flush();return result.data;
        };
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_ASSEMBLY_PREVIEW_WIRE").toStdString());
        const auto saved=assembly::AssemblyDocument::load(source);
        check(!saved.cuts.empty(),"Assembly preview fixture has no cut");
        const auto& cut=saved.cuts.back();const auto id=cut.definition.id;
        const auto packet=[](const auto& edges) {
            kernel::ViewerReferenceGeometry wire;wire.edges=edges;
            return document::serialize_viewer_reference_geometry(wire);
        };
        const auto original_wire=packet(cut.preview_wire);
        check(!cut.preview_wire.empty(),"Assembly fixture has no stored cutter wire");
        window.showMaximized();flush();
        check(window.open_document_path(QString::fromStdString(document::path_to_utf8(source))),"Assembly fixture did not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        check(view&&tree,"Assembly preview View or Tree is missing");
        const auto get=[&]{return run(QString::fromStdString(commands::Json{{"command","feature.get"},{"arguments",{{"container",id}}}}.dump()));};
        const auto original=get();
        const auto row=[&] {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                        (*it)->data(0,Qt::UserRole+3).toString()=="assembly-cut")return *it;
            throw std::runtime_error("Assembly cutter row is missing");
        };
        for(auto* parent=row()->parent();parent;parent=parent->parent())parent->setExpanded(true);
        tree->scrollToItem(row());flush();
        check(!tree->visualItemRect(row()).isEmpty(),"Assembly cut row is not visible for mouse selection");
        const auto point=tree->visualItemRect(row()).center();
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,point,tree->viewport()->mapToGlobal(point),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&event);flush();
        }
        static_cast<void>(view->grabFramebuffer());
        if(view->property("gpuSourceWireVertexCount").toInt()==0) {
            window.grab().save(QString::fromStdString((source.parent_path()/"assembly-selection-failure.png").string()));
            const auto candidate=view->confirmed_candidate();
            std::cerr<<"Assembly selection: expected="<<id<<" row_selected="<<row()->isSelected()
                <<" visible="<<view->isVisible()<<" candidate="<<(candidate?candidate->owner_id:"none")
                <<" path="<<(candidate?candidate->instance_path:"")<<'\n';
        }
        check(view->property("gpuSourceWireVertexCount").toInt()>0,"Single-click Assembly selection has no authored wire");
        window.show_parameter_dimensions(id);flush();
        check(packet(view->container_inspection_wire())==original_wire,"Assembly inspection replaced its cutter wire with component edges");
        check(get().at("revision")==original.at("revision"),"Assembly selection or inspection changed the document");
        const auto edit=[&] {
            const auto scenes=view->base_mesh_revision();
            window.show_tree_item_properties(row());flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog,"Assembly Feature Properties are missing");
            QEventLoop wait;QTimer::singleShot(220,&wait,&QEventLoop::quit);wait.exec();
            check(dialog->property("featureExactPreviewCalculations").toInt()==0,"Assembly Properties opening calculated a cutter");
            check(dialog->property("featureExactPreviewState")=="ready","Assembly Properties did not reuse its stored wire");
            check(packet(view->transient_edges())==original_wire,"Assembly Properties opening changed its stored cutter wire");
            check(view->base_mesh_revision()==scenes+1,"Assembly opening published more than one rollback scene");
            return dialog;
        };
        auto* dialog=edit();const auto scenes=view->base_mesh_revision();
        auto* correction=dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0");
        check(correction,"Assembly correction field is missing");const auto initial=correction->value();
        for(double value:{initial+.1,initial+.2,initial+.25})correction->setValue(value);
        flush();QElapsedTimer timer;timer.start();
        while(dialog->property("featureExactPreviewState")=="pending"&&timer.elapsed()<30000) {
            application.processEvents();QThread::msleep(5);
        }
        check(dialog->property("featureExactPreviewState")=="ready"&&!view->transient_edges().empty(),"Edited Assembly has no exact wire");
        check(dialog->property("featureExactPreviewCalculations").toInt()==1,"Three rapid edits calculated the Assembly preview repeatedly");
        check(view->base_mesh_revision()==scenes,"Assembly correction republished its base scene");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(get().at("revision")==original.at("revision"),"Assembly preview Cancel committed a change");
        dialog=edit();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("revision")==original.at("revision"),"Unchanged Assembly OK created an Undo transaction");
        std::cout<<"Assembly stored preview: real selection, inspection, unchanged opening/OK without calculation, one rollback scene, coalesced corrections and Cancel passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
