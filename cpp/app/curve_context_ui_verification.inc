int verify_curve_context_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::vector<kernel::BodyResult> cached;
        const auto source=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_CURVE_CONTEXT_FILE").toStdWString());
        const auto part=document::PartDocument::load(source,&cached);
        const auto curve=std::ranges::find_if(part.constructions,[](const auto& c){return c.kind==document::ConstructionKind::Curve3D&&c.parent_construction_id.empty();});
        check(curve!=part.constructions.end(),"Curve context fixture has no standalone 3D curve");
        const auto path=directory/"curve-context.prtz";part.save(path,cached);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,950);window.show();
        QTimer watch;bool unexpected=false;
        QObject::connect(&watch,&QTimer::timeout,&window,[&]{for(auto* widget:application.topLevelWidgets())
            if(auto* message=qobject_cast<QMessageBox*>(widget);message&&message->isVisible()){
                std::cerr<<message->text().toStdString()<<std::endl;unexpected=true;message->reject();}});watch.start(100);
        check(window.open_document_path(QString::fromStdString(path.string())),"Curve context fixture cannot open");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"Curve context View/Tree missing");
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
        };
        run("body.activate",{{"body",part.body_owner_for_object(curve->id)->scope.id}});
        const auto triangles=[&]{return view->mesh().triangles.size()/3;};
        const auto before=triangles();check(before>0,"Curve fixture has no visible solid before editing");
        const auto revision=run("feature.get",{{"container",part.history.front().id}}).at("revision");
        const auto edit=[&] {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==curve->id&&
                    (*it)->data(0,Qt::UserRole+3).toString()=="part-construction"){row=*it;break;}
            check(row,"Curve context Tree row missing");window.show_tree_item_properties(row);flush();
            for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<app::ConstructionPropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
            throw std::runtime_error("Curve Properties did not open");
        };
        auto* dialog=edit();
        std::cout<<"Curve context triangles: before="<<before<<" during="<<triangles()<<std::endl;
        view->fit_all();flush();window.grab().save(QString::fromStdString((directory/"curve-context.png").string()));
        check(triangles()>0,"Editing a standalone 3D curve hid the preceding solid");
        check(!unexpected,"Curve Properties reported an error");
        // Exercise each nested point editor: its scene publication and return
        // path differ from opening the curve's top-level Properties directly.
        unsigned coordinate_edits{};
        for(int index=0;index<=static_cast<int>(curve->curve_points.size());++index)for(bool accept:{false,true}) {
            if(index==curve->curve_points.size()&&accept)continue;
            std::cout<<"Curve point editor "<<index<<" accept="<<accept<<std::endl;
            auto* table=dialog->findChild<QTableWidget*>("curve3DPoints");check(table,"Curve point table missing");
            table->scrollToItem(table->item(index,0));flush();
            const auto at=table->visualItemRect(table->item(index,0)).center();
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent mouse(type,QPointF(at),QPointF(table->viewport()->mapToGlobal(at)),Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(table->viewport(),&mouse);
            }flush();
            app::ConstructionPropertiesDialog* child{};
            for(auto* candidate:window.findChildren<QDialog*>())if(candidate!=dialog&&candidate->isVisible())
                if(auto* point=dynamic_cast<app::ConstructionPropertiesDialog*>(candidate))child=point;
            check(child,"Curve point editor did not open");
            std::cout<<"Curve child opened, triangles="<<triangles()<<std::endl;
            check(triangles()>0,"Curve point editor hid preceding material");
            if(index<static_cast<int>(curve->curve_points.size())) {
                const auto position=child->pending_value().origin;
                bool moved=false;
                for(const auto& [key,value]:std::array<std::pair<const char*,double>,3>{{{"x",position.x},{"y",position.y},{"z",position.z}}})
                    if(child->set_inline_parameter_value(key,value+1)) {
                        moved=true;break;
                    }
                if(!moved) {
                    const auto references=child->populated_references();
                    for(std::size_t i=0;i<references.size();++i)
                        if(child->set_inline_parameter_value("reference_offset:"+std::to_string(i),references[i].offset+1)) {moved=true;break;}
                }
                if(moved) {
                    ++coordinate_edits;flush();check(triangles()>0,"Moving a curve point hid preceding material");
                    const auto changed=child->pending_value().origin;
                    check(std::hypot(changed.x-position.x,changed.y-position.y,changed.z-position.z)>1e-7,"Curve point draft did not actually move");
                }
            }
            child->buttons()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
            check(dialog->isVisible()&&triangles()>0,"Returning from a curve point hid preceding material");
            std::cout<<"Curve child returned"<<std::endl;
        }
        auto* rounding=dialog->findChild<QCheckBox*>("curve3DRounding");check(rounding,"Curve rounding control missing");
        std::cout<<"Curve rounding drafts"<<std::endl;
        const bool rounded=rounding->isChecked();rounding->setChecked(!rounded);flush();
        check(triangles()>0,"Toggling curve rounding hid preceding material");rounding->setChecked(rounded);flush();
        auto* type=dialog->findChild<QComboBox*>("curve3DType");check(type,"Curve type control missing");
        type->setCurrentIndex(type->findData(static_cast<int>(document::Curve3DType::Polyline)));flush();
        rounding=dialog->findChild<QCheckBox*>("curve3DRounding");rounding->setChecked(true);flush();
        unsigned radius_edits{};
        for(int index=0;index<static_cast<int>(curve->curve_points.size());++index) {
            if(auto* flip=dialog->findChild<QCheckBox*>(QString("curve3DFlip%1").arg(index));flip&&flip->isEnabled()) {
                std::cout<<"Curve tangent draft "<<index<<std::endl;
                const bool initial=flip->isChecked();flip->setChecked(!initial);flush();
                check(triangles()>0,"Changing a curve tangent hid preceding material");
                // Changing an axis rebuilds the point table and retires its widgets.
                flip=dialog->findChild<QCheckBox*>(QString("curve3DFlip%1").arg(index));
                check(flip,"Curve tangent control disappeared after refresh");flip->setChecked(initial);flush();
            }
            if(auto* radius=dialog->findChild<QDoubleSpinBox*>(QString("curve3DRadius%1").arg(index+1));radius&&radius->isEnabled()) {
                ++radius_edits;
                std::cout<<"Curve radius draft "<<index<<std::endl;
                const auto value=radius->value();radius->setValue(value+1);flush();
                check(triangles()>0,"Changing a curve radius hid preceding material");
                radius=dialog->findChild<QDoubleSpinBox*>(QString("curve3DRadius%1").arg(index+1));
                check(radius,"Curve radius control disappeared after refresh");radius->setValue(value);flush();
            }
        }
        check(radius_edits>0,"Curve radius variants were not exercised");
        check(coordinate_edits>0,"Curve point-coordinate variants were not exercised");
        std::cout<<"Curve context: "<<coordinate_edits<<" point-coordinate drafts and "<<radius_edits<<" radius drafts"<<std::endl;
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(triangles()==before,"Curve Cancel did not restore the complete model");
        check(run("feature.get",{{"container",part.history.front().id}}).at("revision")==revision,"Curve Cancel changed document revision");
        dialog=edit();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!unexpected&&triangles()==before,"Unchanged Curve OK lost the complete model");
        check(run("feature.get",{{"container",part.history.front().id}}).at("revision")==revision,"Unchanged Curve OK created an Undo step");
        run("save");run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Curve native reopen failed");flush();
        check(triangles()==before,"Curve native reopen changed solid display");
        dialog=edit();check(triangles()>0,"Reopened Curve edit lost the preceding solid");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        std::cout<<"Curve input context, all point editors, new-point Cancel, tangent/rounding/radius drafts, Cancel, unchanged OK and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
