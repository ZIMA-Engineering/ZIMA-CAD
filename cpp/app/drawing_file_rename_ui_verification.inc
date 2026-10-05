Q_NEVER_INLINE static void verify_drawing_file_rename_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()){
        auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);flush();return result;
    };
    const auto folder=std::filesystem::absolute(directory)/("drawing-rename-"+document::PartDocument::create_default().document_id);
    std::filesystem::create_directories(folder);
    run("cd",{{"path",document::path_to_utf8(folder)}});
    run("new",{{"type","part"},{"name","SOURCE"}});
    check(test::gui_rectangular_profile(window,10,20,30).ok,"Drawing rename Part fixture failed");
    run("save");
    const auto source=folder/"SOURCE.prtz";
    std::vector<kernel::BodyResult> bodies;
    const auto part=document::PartDocument::load(source,&bodies);
    check(!bodies.empty(),"Drawing rename has no calculated source");
    auto assembly=assembly::AssemblyDocument::create_default();
    for(int i=0;i<2;++i)assembly.components.push_back(assembly::AssemblyDocument::create_part_occurrence("Repeated",part.document_id,source,bodies.back()));
    const auto assembly_path=folder/"OWNER.asmz";assembly.save(assembly_path);
    auto drawing=drawing::DrawingDocument::create_default();
    drawing.source_document_id=part.document_id;drawing.source_path=source;drawing.source_name=part.name;
    drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(part.document_id,source,bodies.back().mesh));
    const auto drawing_path=folder/"DIFFERENT-DRAWING.drwz";drawing.save(drawing_path);
    check(window.open_document_path(QString::fromStdString(document::path_to_utf8(assembly_path))),"Cannot open owning Assembly");flush();
    check(window.open_document_path(QString::fromStdString(document::path_to_utf8(drawing_path))),"Cannot open linked Drawing");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"Drawing Rename Tree missing");
    for(const auto& id:{drawing.sheets.front().id,drawing.sheets.front().views.front().id}) {
        QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){row=*it;break;}
        check(row,"Drawing sheet/view row missing");for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
        tree->scrollToItem(row);flush();bool offered=false;
        QTimer::singleShot(0,&window,[&]{auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
            if(auto* rename=menu->findChild<QAction*>("renameTreeItemAction")){offered=true;rename->trigger();}menu->close();});
        QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(row).center()));flush();
        check(offered,"Drawing sheet/view Rename is missing from its selection menu");
        auto* rename=window.findChild<QDialog*>("renameTreeItemDialog");check(rename&&rename->isVisible(),"Drawing item Rename did not open");
        rename->findChild<QLineEdit*>("renameTreeItemName")->setText("Independent label");
        rename->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        bool renamed=false;for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole).toString().toStdString()==id)renamed=(*it)->text(0).contains("Independent label");
        check(renamed&&std::filesystem::exists(source)&&std::filesystem::exists(drawing_path),"Item Rename did not refresh or changed a native filename");
        window.findChild<QAction*>("undoAction")->trigger();flush();
    }
    auto* action=window.findChild<QAction*>("renameDocumentAction");check(action&&action->isEnabled(),"Drawing File Rename unavailable");
    const auto open=[&]{action->trigger();flush();auto* dialog=window.findChild<QDialog*>("renameDocumentDialog");
        check(dialog&&dialog->isVisible(),"Drawing Rename did not open");
        check(dialog->findChild<QLineEdit*>("renameDocumentName")->text()=="DIFFERENT-DRAWING.drwz","Drawing Rename targets another active document");
        return QPointer<QDialog>(dialog);
    };
    auto dialog=open();dialog->findChild<QLineEdit*>("renameDocumentName")->setText("RENAMED.drwz");
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
    check(std::filesystem::exists(source)&&std::filesystem::exists(drawing_path)&&!std::filesystem::exists(folder/"RENAMED.prtz"),"Drawing Rename Cancel changed files");
    dialog=open();dialog->findChild<QLineEdit*>("renameDocumentName")->setText("RENAMED.drwz");
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
    const auto model_target=folder/"RENAMED.prtz",drawing_target=folder/"RENAMED.drwz";
    check((!dialog||!dialog->isVisible())&&std::filesystem::exists(model_target)&&std::filesystem::exists(drawing_target)&&
        !std::filesystem::exists(source)&&!std::filesystem::exists(drawing_path),"Drawing Rename did not commit both files");
    auto* tabs=window.findChild<QTabBar*>("documentTabs");check(tabs,"Drawing Rename tabs missing");
    bool part_tab=false,drawing_tab=false;int assembly_tab=-1;
    for(int i=0;i<tabs->count();++i){const auto id=tabs->tabData(i).toString().toStdString();
        if(id==part.document_id)part_tab=tabs->tabText(i)=="RENAMED.prtz";
        if(id==drawing.document_id)drawing_tab=tabs->tabText(i)=="RENAMED.drwz";
        if(id==assembly.document_id)assembly_tab=i;
    }
    check(part_tab&&drawing_tab&&assembly_tab>=0,"Drawing Rename missed model/drawing tab titles");
    tabs->setCurrentIndex(assembly_tab);flush();
    for(const auto& occurrence:assembly.components){bool found=false;for(QTreeWidgetItemIterator it(tree);*it;++it)
        if((*it)->data(0,Qt::UserRole+1).toString().toStdString()==assembly::InstancePath{{occurrence.occurrence_id}}.encoded()&&
                (*it)->data(0,Qt::UserRole+3).toString()=="part-occurrence"){
            found=(*it)->text(0).contains("RENAMED");break;}
        check(found,"Drawing Rename missed an open Assembly occurrence label");
    }
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto saved_assembly=assembly::AssemblyDocument::load(assembly_path);
    for(std::size_t i=0;i<assembly.components.size();++i)check(saved_assembly.components[i].source_path==model_target&&
        saved_assembly.components[i].occurrence_id==assembly.components[i].occurrence_id,"Drawing Rename lost an Assembly occurrence reference");
    std::vector<kernel::BodyResult> reopened;
    const auto saved_part=document::PartDocument::load(model_target,&reopened);
    const auto saved_drawing=drawing::DrawingDocument::load(drawing_target);
    check(saved_part.document_id==part.document_id&&!reopened.empty()&&reopened.back().volume==bodies.back().volume&&
        saved_drawing.source_document_id==part.document_id&&saved_drawing.source_path==model_target&&
        saved_drawing.sheets.front().views.front().source_path==model_target,"Paired Drawing Rename changed geometry/identity or missed persisted sources");
    std::cout<<"Drawing File Rename, Cancel, paired Part, repeated open Assembly, tabs and native reopen passed\n";
}
