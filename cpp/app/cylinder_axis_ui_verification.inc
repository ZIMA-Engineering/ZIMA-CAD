int verify_cylinder_axis_ui(QApplication& application, zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
        auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",arguments}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);return result.data;
    };
    try {
        window.resize(1366,768);window.show();flush();
        kernel::OcctKernel kernel;
        auto source=document::PartDocument::create_default();
        auto cylinder=document::PartDocument::create_cylinder_container();
        cylinder.cylinder.radius=8;cylinder.cylinder.height=25;
        source.insert_history_entry(document::PartHistoryKind::Feature,cylinder.id);source.history.push_back(cylinder);
        const auto step=directory/"axis-ui.step";
        kernel.export_step(interchange::step_product(source,kernel.evaluate_history(source.kernel_operations())),document::path_to_utf8(step));
        auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
        const auto import_id=imported.document.history.front().id;
        const auto file=directory/"axis-ui.prtz";imported.document.save(file,imported.calculated);
        check(window.open_document_path(QString::fromStdString(file.string())),"Cannot open axis fixture");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* action=window.findChild<QAction*>("cylinderAxisAction");
        check(view&&tree&&action&&action->isEnabled()&&!action->icon().isNull(),"Cylinder axis action is missing");
        auto* appearance=window.findChild<QAction*>("customBodyColorAction");
        check(appearance&&qobject_cast<QMenu*>(appearance->parent())&&
            !window.findChild<QMenu*>("colorsMenu"),"Appearance is still nested in a one-item Colors submenu");
        view->set_view_direction({1,-1,0.5});view->fit_all();flush();
        const auto dialog=[&] {
            for (auto* widget : window.findChildren<QDialog*>())
                if (auto* result=dynamic_cast<app::CylinderAxisDialog*>(widget);result&&result->isVisible()) return result;
            throw std::runtime_error("Cylinder axis Properties did not open");
        };
        const auto pick=[&] {
            std::optional<QPointF> hit;
            for(int y=8;y<view->height()&&!hit;y+=6)for(int x=8;x<view->width();x+=6) {
                const QPointF point(x,y);const auto offered=view->selection_candidates_at(point);
                if(!offered.empty()&&offered.front().owner_id==import_id&&offered.front().kind==viewer::CandidateKind::Face) {hit=point;break;}
            }
            check(hit.has_value(),"Common picker did not offer an imported cylindrical face");
            const auto p=*hit;
            QMouseEvent move(QEvent::MouseMove,p,p,p,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
            QMouseEvent press(QEvent::MouseButtonPress,p,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,p,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
            check(dialog()->pending().references.size()==1,"Cylinder face click was not accepted");
            check(!dialog()->findChild<QTableWidget*>("cylinderAxisReference")->item(0,1)->text().isEmpty(),"Selected cylinder has no visible field label");
        };
        const auto command_button=[&]() -> QToolButton* {
            for (auto* button : window.findChildren<QToolButton*>())
                if (button->isVisible() && button->defaultAction()==action) return button;
            throw std::runtime_error("Cylinder axis toolbar button is missing");
        };
        {
            auto* button=command_button();
            const auto position=button->rect().center();
            QCursor::setPos(button->mapToGlobal(position));flush();
            QMouseEvent press(QEvent::MouseButtonPress,position,position,button->mapToGlobal(position),
                Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,position,position,button->mapToGlobal(position),
                Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(button,&press);QApplication::sendEvent(button,&release);flush();
            auto* current=dialog();
            const auto away=view->rect().center();
            QCursor::setPos(view->mapToGlobal(away));
            QMouseEvent move(QEvent::MouseMove,away,away,view->mapToGlobal(away),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            button=command_button();
            check(!button->underMouse()&&!button->isDown(),"Cylinder axis retains hover/press after moving to View");
            const auto image=button->grab().toImage();
            check(image.pixelColor(qRound((button->width()-8)*image.devicePixelRatio()),
                qRound(button->height()/2.0*image.devicePixelRatio()))==QColor("#00D1FF"),
                "Cylinder axis command is not azure while Properties are open");
            current->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            check(!action->property("zimaCommandActive").toBool(),"Closed command retained active feedback");
            window.grab().save(QString::fromStdString((directory/"interaction-colors.png").string()));
        }
        const auto settings_before=app::ApplicationSettings::load();QTemporaryDir languages;
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(languages.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));config.sync();
            const auto settings=app::ApplicationSettings::load(languages.path());app::apply_application_translations(application,settings);
            action->trigger();flush();auto* current=dialog();
            check(current->windowTitle()==settings.qt_translations.value("Vlastnosti osy válcové plochy"),"Axis properties title untranslated");
            check(QString::fromStdString(current->pending().name)==settings.qt_translations.value("Osa válcové plochy"),"New axis name untranslated");
            check(!current->findChild<QPushButton*>("containerOriginSelectionButton"),"Cylinder axis exposes unnecessary Origin button");
            auto* table=current->findChild<QTableWidget*>("cylinderAxisReference");
            auto* indicator=table->cellWidget(0,0);
            auto* arrow=qobject_cast<QWidget*>(indicator->property("_arrowWidget").value<QObject*>());
            check(arrow&&arrow->isVisible()&&arrow->toolTip()==settings.qt_translations.value("Zadejte referenci"),"Axis reference arrow missing or untranslated");
            const auto top=table->mapTo(current,QPoint{}).y();
            current->resize(current->width()+80,current->height()+150);flush();
            check(table->mapTo(current,QPoint{}).y()==top,"Axis reference moved on resize");
            current->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        }
        app::apply_application_translations(application,settings_before);
        std::cerr<<"Axis UI stage: first creation\n";
        action->trigger();flush();dialog();pick();
        auto* references=dialog()->findChild<QTableWidget*>("cylinderAxisReference");
        auto* remove=qobject_cast<QPushButton*>(references->cellWidget(0,0)->property("_removeWidget").value<QObject*>());
        check(remove&&remove->isVisible(),"Selected axis reference has no remove button");
        remove->click();flush();
        check(dialog()->pending().references.empty()&&dialog()->active()&&!dialog()->inspected(),"Axis reference removal did not rearm entry");
        pick();
        check(run("construction.list").at("total")==0,"Axis preview committed before OK");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run("construction.list").at("total")==0,"Cancel created an axis");
        std::cerr<<"Axis UI stage: second creation\n";
        action->trigger();flush();dialog();pick();
        const auto axis_id=dialog()->pending().id;
        check(std::ranges::any_of(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==dialog()->pending().entity_id;}),
            "Axis preview was not rendered");
        window.grab().save(QString::fromStdString((directory/"cylinder-axis-preview.png").string()));
        QMouseEvent middle(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(20,20),QPointF(20,20),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&middle);flush();
        check(run("construction.list").at("total")==1,"Middle double-click failed to commit axis");
        const auto query=[&]{return run("construction.get",{{"construction",axis_id}});};
        check(query().at("definition")=="cylinder_axis"&&query().at("reference_valid")==true,"Axis definition or source is lost");
        QTreeWidgetItem* item=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==axis_id){item=*it;break;}
        std::cerr<<"Axis UI stage: reopen\n";
        check(item,"Axis is absent from Tree");window.show_tree_item_properties(item);flush();
        check(dialog()->pending().references.size()==1,"Reopened axis lost source");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        run("import.set",{{"container",import_id},{"placement",{{"x",17},{"rotation_x",30}}}});flush();
        check(std::abs(query().at("origin_mm")[0].get<double>()-17)<1e-6,"Axis did not follow import move");
        run("undo");run("redo");run("save");
        check(document::PartDocument::load(file).find_construction(axis_id)->reference_valid,"Saved axis is invalid");
        // Opening import Properties must retain a wire at its current position.
        item=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==import_id){item=*it;break;}
        check(item,"Imported container is absent from Tree");window.show_tree_item_properties(item);flush();
        check(!view->transient_edges().empty(),"Imported Properties hid the complete source wire");
        window.grab().save(QString::fromStdString((directory/"import-placement-preview.png").string()));
        for(auto* widget:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);properties&&properties->isVisible()) {
            const auto minimum=[&] {
                double x=std::numeric_limits<double>::infinity();
                for(const auto& edge:view->transient_edges())for(const auto& p:edge.points)x=std::min(x,p.x);
                return x;
            };
            const double before=minimum();
            auto* x=properties->findChild<QDoubleSpinBox*>("primitiveTranslation");
            check(x&&x->isEnabled(),"Import placement X is not editable");
            x->setValue(x->value()+5);flush();
            check(std::abs(minimum()-before-5)<1e-6,"Imported wire did not follow pending numeric placement");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
        }
        flush();
        // Place repeated imported components directly by their cylindrical faces.
        std::vector<kernel::BodyResult> cache;
        const auto source_part=document::PartDocument::load(file,&cache);
        auto group=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("Fixed",source_part.document_id,file,cache.back());
        auto sub=assembly::AssemblyDocument::create_default();
        auto nested_leaf=assembly::AssemblyDocument::create_part_occurrence("Nested cylinder",source_part.document_id,file,cache.back());
        nested_leaf.placement.rotation_z=17;sub.components={nested_leaf};
        const auto sub_file=directory/"cylinder-mate-subassembly.asmz";sub.save(sub_file);
        auto moving=assembly::AssemblyDocument::create_assembly_occurrence("Moving subassembly",sub.document_id,sub_file,sub);
        fixed.grounded=true;moving.placement.x=55;
        group.components={fixed,moving};const auto group_file=directory/"cylinder-mate-ui.asmz";group.save(group_file);
        check(window.open_document_path(QString::fromStdString(group_file.string())),"Cannot open cylinder mate assembly");flush();
        view->set_view_direction({0,-1,0.3});view->fit_all();flush();
        const auto find_component=[&]() -> QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==moving.occurrence_id)return *it;
            throw std::runtime_error("Cylinder mate component tree row missing");
        };
        const auto component_dialog=[&]() -> app::ComponentPropertiesDialog* {
            for(auto* widget:window.findChildren<QDialog*>())
                if(auto* p=dynamic_cast<app::ComponentPropertiesDialog*>(widget);p&&p->isVisible())return p;
            throw std::runtime_error("Component properties missing for cylinder mate");
        };
        window.show_tree_item_properties(find_component());flush();
        const auto pick_cylinder=[&](const std::string& occurrence,bool source_side) {
            component_dialog()->findChild<QTableWidget*>("componentPlacementTable")->cellClicked(0,source_side?1:2);flush();
            std::optional<QPointF> hit;
            for(int y=8;y<view->height()&&!hit;y+=5)for(int x=8;x<view->width();x+=5) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                if(candidates.empty())continue;
                const auto& c=candidates.front();
                auto face_path=assembly::InstancePath{{occurrence}};
                if(occurrence==moving.occurrence_id)face_path=face_path.child(nested_leaf.occurrence_id);
                if(c.kind!=viewer::CandidateKind::Face||c.instance_path!=face_path.encoded())continue;
                const auto& faces=c.geometry==viewer::CandidateGeometry::OriginalReference?view->mesh().original_references.triangle_references:view->mesh().triangle_references;
                if(c.geometry_index<faces.size()&&faces[c.geometry_index].surface&&faces[c.geometry_index].surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){hit=QPointF(x,y);break;}
            }
            check(hit.has_value(),"Common component picker did not offer cylindrical face");
            for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),
                    type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
            const auto& row=component_dialog()->placement_references().at(0);
            check((source_side?row.component_reference:row.target_reference).kind==assembly::MateReferenceKind::CylinderFace,
                "Cylindrical face was stored as a planar reference");
        };
        pick_cylinder(moving.occurrence_id,true);pick_cylinder(fixed.occurrence_id,false);
        check(component_dialog()->placement_references()[0].mate_type==assembly::MateKind::AxisCoincident,"Cylinder pair did not reuse axis mate");
        window.grab().save(QString::fromStdString((directory/"cylinder-mate-preview.png").string()));
        component_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.show_tree_item_properties(find_component());flush();
        check(component_dialog()->placement_references().empty(),"Cancel persisted cylinder mating");
        pick_cylinder(moving.occurrence_id,true);pick_cylinder(fixed.occurrence_id,false);
        component_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        run("undo");run("redo");run("save");
        const auto saved_group=assembly::AssemblyDocument::load(group_file);
        check(saved_group.components[1].placement_references[0].component_reference.kind==assembly::MateReferenceKind::CylinderFace,
            "Cylinder mating did not survive Undo/Redo/save");
        check(saved_group.component_constraint_state(moving.occurrence_id).remaining_dof==2,"Cylinder mating changed remaining motion");
        check(assembly::AssemblyDocument::load(sub_file).components[0].placement==nested_leaf.placement,
            "Parent cylinder mate changed internal subassembly placement");
        window.show_tree_item_properties(find_component());flush();
        check(component_dialog()->placement_references()[0].target_reference.kind==assembly::MateReferenceKind::CylinderFace,"Reopening lost target cylinder");
        component_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        std::cout<<"Cylinder axis UI: common face picker, preview, Cancel, middle-button OK, reopen, source move and native save passed\n";
        return 0;
    }catch(const std::exception& error){
        window.grab().save(QString::fromStdString((directory/"cylinder-axis-failure.png").string()));
        for(auto* widget:window.findChildren<QDialog*>())std::cerr<<"Dialog: "<<widget->objectName().toStdString()<<" visible="<<widget->isVisible()<<'\n';
        std::cerr<<"Cylinder axis UI: "<<error.what()<<'\n';return 1;}
}
