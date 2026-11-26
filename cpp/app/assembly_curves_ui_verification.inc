static int verify_assembly_curves_ui(QApplication& application,AssemblyWorkspaceWindow& window) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button=Qt::LeftButton) {
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&event);flush();
            }
        };
        const auto path=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_ASSEMBLY_CURVE_MODEL").toStdWString());
        const auto original=assembly::AssemblyDocument::load(path);
        window.resize(1440,1000);window.show();check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open Assembly curves fixture");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Assembly curves View/Tree missing");
        const auto editor=[&]()->PrimitivePropertiesDialog* {for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;return nullptr;};
        const auto state=[&]{return window.execute_console_command("documents").data;};
        const auto before=state();const auto calls=kernel::body_calculation_count.load();
        for(const auto* name:{"assemblyProjectCurveAction","assemblyWrapCurveAction"}) {
            auto* action=window.findChild<QAction*>(name);check(action&&action->isEnabled()&&!action->icon().isNull(),"Assembly curve command missing");action->trigger();flush();check(editor(),"Assembly curve creation did not open");
            check(!editor()->findChild<QListWidget*>("assemblyCutTargets")->isVisible(),"Reference curve offers material target components");
            auto* output=editor()->findChild<QComboBox*>(std::string_view(name)=="assemblyWrapCurveAction"?"wrappingOutput":"projectionOutput");
            check(output&&output->currentIndex()==0&&!output->model()->flags(output->model()->index(1,0)).testFlag(Qt::ItemIsEnabled),"Assembly curve offers material conversion");
            editor()->reject();flush();check(state()==before,"Cancelled Assembly curve created history");
        }
        check(kernel::body_calculation_count.load()==calls,"Opening reference curve commands calculated geometry");
        for(const auto& cut:original.cuts)if(assembly::is_reference_curve_feature(cut.definition)) {
            const auto id=cut.definition.id;const bool wrap=cut.definition.feature.type==document::FeatureType::Wrapping;
            const auto target=wrap?cut.definition.feature.wrapping.target->reference:cut.definition.feature.projection.targets.front().reference;
            const auto row=[&]()->QTreeWidgetItem* {for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="assembly-cut"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;return nullptr;};
            check(row(),"Reference curve Tree row missing");window.show_tree_item_properties(row());flush();check(editor(),"Reference curve Properties missing");
            auto* table=editor()->findChild<QTableWidget*>(wrap?"wrappingTarget":"projectionTargets");check(table,"Curve target table missing");
            for(auto* ancestor=table->parentWidget();ancestor;ancestor=ancestor->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(table);
            flush();click(table->viewport(),table->visualItemRect(table->item(0,2)).center());view->fit_all();flush();
            std::optional<QPointF> position;
            for(int y=24;y<view->height()-24&&!position;y+=8)for(int x=24;x<view->width()-24&&!position;x+=8) {
                QPointF pixel(x,y);for(const auto& candidate:view->selection_candidates_at(pixel))
                    if(candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==target.owner_id&&candidate.semantic_key==target.semantic_key&&candidate.instance_path==target.instance_path){position=pixel;break;}
            }
            check(position.has_value(),"Exact component face not offered for Assembly curve");
            QMouseEvent move(QEvent::MouseMove,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
            for(std::size_t i=0;i<view->selection_candidates_at(*position).size();++i) {
                const auto offered=view->offered_candidate();if(offered&&offered->owner_id==target.owner_id&&offered->semantic_key==target.semantic_key&&offered->instance_path==target.instance_path)break;
                click(view,*position,Qt::RightButton);
            }
            click(view,*position);flush();
            const auto pending=editor()->pending_value();const auto selected=wrap?pending.feature.wrapping.target->reference:pending.feature.projection.targets.front().reference;
            check(selected==target,"View selection changed source occurrence identity");
            check(!view->transient_edges().empty(),"Assembly curve has no draft wire preview");
            table->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
            editor()->reject();flush();check(state()==before&&kernel::body_calculation_count.load()==calls,"Reference selection/Cancel calculated or committed geometry");
            window.show_tree_item_properties(row());flush();check(editor(),"Reference curve reopening failed");
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor())throw std::runtime_error("Reference curve unchanged OK: "+editor()->findChild<QLabel*>("featureValidationError")->text().toStdString());
            check(state()==before&&kernel::body_calculation_count.load()==calls,"Reference curve unchanged OK recalculated or committed");
        }
        std::cout<<"Assembly curves GUI: creation, material guards, actual face picking, inspection, preview, Cancel and no-op OK passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Assembly curves GUI: "<<error.what()<<'\n';return 1;}
}
