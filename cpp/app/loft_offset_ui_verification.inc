// Called inside the ordinary Loft GUI verifier with an isolated authored
// rectangle/circle/slot fixture produced by the native document regression.
const auto source=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_MIXED_LOFT_FILE").toStdU16String());
std::vector<kernel::BodyResult> input;
const auto original=zima::test::load_part_with_properties(source,&input);
const auto feature=std::ranges::find_if(original.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Loft;});
check(feature!=original.history.end()&&!input.empty(),"Mixed Loft GUI fixture is incomplete");
const auto id=feature->id;
const auto path=directory/"mixed-offset-ui.prtz";original.save(path,input);
check(window.open_document_path(QString::fromStdU16String(path.u16string())),"Mixed Loft GUI fixture cannot open");flush();
commands::Json arguments={{"document",original.document_id},{"body",original.body_history.bodies().front().scope.id}};
check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",arguments}}.dump())).ok,
    "Mixed Loft GUI Body cannot activate");flush();
auto* tree=window.findChild<QTreeWidget*>("documentTree");
auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(tree&&view,"Mixed Loft View/Tree missing");
const auto row=[&](const std::string& owner)->QTreeWidgetItem* {
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3).toString()=="part-container")return *it;
    return nullptr;
};
const auto editor=[&] {return dynamic_cast<LoftDialog*>(window.findChild<QWidget*>("loftDialog"));};
const auto edit=[&] {check(row(id),"Mixed Loft Tree row missing");window.show_tree_item_properties(row(id));flush();check(editor(),"Mixed Loft Properties missing");return editor();};
const auto save=[&] {check(window.execute_console_command("save").ok,"Mixed offset GUI Save failed");flush();};
const auto state=[&] {return window.execute_console_command("documents").data;};
const auto verify_result=[&](bool thin) {
    save();std::vector<kernel::BodyResult> cache;const auto saved=zima::test::load_part_with_properties(path,&cache);
    check(!cache.empty()&&cache.back().calculation_errors.empty()&&cache.back().has_solid()&&
        cache.back().volume>0&&cache.back().volume<input.back().volume*.5,"Mixed offset GUI did not save a hollow valid result");
    if(thin)check(saved.find_container(id)->loft.result_type==document::ProfileResultType::Thin,"Thin result type was not saved");
    return saved;
};
for(int side=0;side<3;++side) {
    const auto before=state();auto* dialog=edit();
    const auto count=kernel::body_calculation_count.load();
    dialog->findChild<QComboBox*>("loftResultType")->setCurrentIndex(1);
    dialog->findChild<QComboBox*>("loftThinSide")->setCurrentIndex(side);
    dialog->findChild<QDoubleSpinBox*>("loftThickness")->setValue(.5);flush();
    check(kernel::body_calculation_count.load()==count,"Mixed Thin draft calculated geometry");
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(state()==before,"Mixed Thin Cancel changed the document");
    dialog=edit();dialog->findChild<QComboBox*>("loftResultType")->setCurrentIndex(1);
    dialog->findChild<QComboBox*>("loftThinSide")->setCurrentIndex(side);
    dialog->findChild<QDoubleSpinBox*>("loftThickness")->setValue(1.);
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    if(auto* failed=editor())throw std::runtime_error("Mixed Thin GUI OK failed: "+failed->findChild<QLabel*>("loftStatus")->text().toStdString());
    const auto saved=verify_result(true);
    view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();flush();
    window.grab().save(QString::fromStdU16String((directory/("mixed-thin-side-"+std::to_string(side)+".png")).u16string()));
    const auto committed=state();dialog=edit();const auto no_op=kernel::body_calculation_count.load();
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(!editor()&&state()==committed&&kernel::body_calculation_count.load()==no_op,"Mixed Thin unchanged OK recalculated or committed");
    window.findChild<QAction*>("undoAction")->trigger();flush();
    window.findChild<QAction*>("redoAction")->trigger();flush();
    check(verify_result(true).find_container(id)->loft==saved.find_container(id)->loft,"Mixed Thin GUI Undo/Redo lost the definition");
}
// Restore the full carrier through the same Properties dialog, then create
// Shell using its ordinary independent reference table and command callbacks.
auto* full=edit();full->findChild<QComboBox*>("loftResultType")->setCurrentIndex(0);
full->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Mixed Loft could not return to Solid");
const auto shell_dialog=[&]()->PrimitivePropertiesDialog* {
    for(auto* widget:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(widget);
        dialog&&dialog->isVisible()&&dialog->findChild<QDoubleSpinBox*>("shellThickness"))return dialog;
    return nullptr;
};
auto* shell_action=window.findChild<QAction*>("shellAction");check(shell_action&&shell_action->isEnabled(),"Mixed Loft Shell command unavailable");
shell_action->trigger();flush();check(shell_dialog(),"Mixed Loft Shell Properties missing");
const std::vector<kernel::FaceReference> caps{{id,"loft:cap:from:"+feature->loft.profiles.front(),{}},
    {id,"loft:cap:from:"+feature->loft.profiles.back(),{}}};
const auto shell_preview_count=kernel::body_calculation_count.load();
for(unsigned end=0;end<2;++end) {
    auto* table=shell_dialog()->findChild<QTableWidget*>("shellFaces");
    click(table->viewport(),table->visualItemRect(table->item(table->rowCount()-1,1)).center());
    std::optional<QPoint> hit;std::set<std::string> offered_faces;
    for(double direction:{end==0?-1.:1.,end==0?1.:-1.}) {
        view->set_view_direction({0,0,direction});view->fit_all();flush();
        {QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();}
        for(int y=20;y<view->height()-20&&!hit;y+=8)for(int x=20;x<view->width()-20&&!hit;x+=8) {
            const auto offered=view->selection_candidates_at(QPointF(x,y));
            for(const auto& candidate:offered)offered_faces.insert(std::to_string(int(candidate.kind))+":"+candidate.owner_id+":"+candidate.semantic_key);
            if(!offered.empty()&&offered.front().kind==viewer::CandidateKind::Face&&
                offered.front().owner_id==id&&offered.front().semantic_key==caps[end].semantic_key)hit=QPoint(x,y);
        }
        if(hit)break;
    }
    if(!hit) {
        window.grab().save(QString::fromStdU16String((directory/"mixed-shell-pick-failure.png").u16string()));
        std::cerr<<"Wanted end "<<end<<": "<<id<<":"<<caps[end].semantic_key<<std::endl;
        for(const auto& candidate:offered_faces)std::cerr<<candidate<<std::endl;
    }
    check(hit.has_value(),"Mixed Loft cap cannot be selected for Shell in View");click(view,*hit);
    check(shell_dialog()->pending_value().shell.removed_faces.size()==end+1,
        "Shell mouse selection did not populate its reference table");
}
check(shell_dialog()->pending_value().shell.removed_faces==caps&&kernel::body_calculation_count.load()==shell_preview_count,
    "Shell cap picking changed reference identity or calculated preview geometry");
shell_dialog()->findChild<QDoubleSpinBox*>("shellThickness")->setValue(1.);flush();
const auto shell_id=shell_dialog()->pending_value().id;
shell_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!shell_dialog(),"Mixed Loft Shell OK failed");
const auto shelled=verify_result(false);check(shelled.find_container(shell_id)->shell.removed_faces==caps,"Shell GUI lost selected end caps");
view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();flush();window.grab().save(QString::fromStdU16String((directory/"mixed-shell.png").u16string()));
const auto shell_state=state();window.show_tree_item_properties(row(shell_id));flush();check(shell_dialog(),"Mixed Shell edit missing");
shell_dialog()->findChild<QDoubleSpinBox*>("shellThickness")->setValue(2.);
shell_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(state()==shell_state,"Mixed Shell Cancel changed its result");
window.show_tree_item_properties(row(shell_id));flush();check(shell_dialog(),"Mixed Shell unchanged edit missing");
const auto no_op=kernel::body_calculation_count.load();shell_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
check(!shell_dialog()&&state()==shell_state&&kernel::body_calculation_count.load()==no_op,"Mixed Shell unchanged OK recalculated or committed");
window.findChild<QAction*>("undoAction")->trigger();flush();save();check(!document::PartDocument::load(path).find_container(shell_id),"Mixed Shell GUI Undo retained Shell");
window.findChild<QAction*>("redoAction")->trigger();flush();check(verify_result(false).find_container(shell_id)->shell==shelled.find_container(shell_id)->shell,"Mixed Shell GUI Redo lost Shell");
std::cout<<"Mixed Loft GUI: three Thin sides, preview without calculation, OK/Cancel/no-op, native Save, Undo/Redo, Shell end-cap mouse selection and screenshots passed"<<std::endl;
return 0;
