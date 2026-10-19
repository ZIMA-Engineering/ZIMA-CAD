int verify_opening_copy_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{for(int i=0;i<8;++i){application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}};
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();window.activateWindow();flush();
        window.findChild<QAction*>("applicationModeAction0")->trigger();flush();
        const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",arguments}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
        };
        run("new",{{"type","part"},{"name","opening-copy-ui"}});
        const auto sketch=run("sketch.create",{{"name","Clipboard fixture block"},{"plane","XY"}}).at("sketch");
        run("sketch.rectangle.create",{{"sketch",sketch},{"first",{-30,-30}},{"second",{30,30}}});
        document::FeatureParameters block;block.sides[0].length=60;
        auto parameters=document::serialize_feature_parameters(block);parameters.erase("sketch_id");
        const auto blank=run("feature.create",{{"sketch",sketch},{"parameters",parameters}}).at("container").get<std::string>();
        const auto opening=run("opening.create",{{"type","metric"},{"designation","M10"},{"bore_length_mm",20},{"thread_length_mm",10},
            {"chamfer_enabled",true},{"chamfer_depth_mm",.6},{"chamfer_angle_degrees",90},{"drill_point_enabled",true},{"placement",{{"x",-15}}}}).at("container").get<std::string>();
        const auto path=directory/"copied-openings.prtz";
        run("save_as",{{"path",path.generic_string()}});run("close",{{"discard",true}});run("open",{{"path",path.generic_string()}});
        const auto initial=document::PartDocument::load(path);const auto source=*initial.find_container(opening);
        check(activate_test_body(application,window,initial.body_history.active_body_id()),"Copy fixture Body activation failed");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        const auto select=[&](const std::string& id) {
            QTreeWidgetItem* row=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)
                if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row,"Copy fixture feature Tree row missing");tree->clearSelection();tree->setCurrentItem(row);row->setSelected(true);tree->setFocus();flush();
        };
        const auto key=[&](QWidget* widget,int code) {
            window.activateWindow();QApplication::setActiveWindow(&window);widget->setFocus();flush();
            QKeyEvent press(QEvent::KeyPress,code,Qt::ControlModifier),release(QEvent::KeyRelease,code,Qt::ControlModifier);
            QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);flush();
        };
        const auto dialog=[&]() -> app::PrimitivePropertiesDialog* {
            for(auto* value:window.findChildren<QDialog*>())if(value->isVisible())
                if(auto* properties=dynamic_cast<app::PrimitivePropertiesDialog*>(value))return properties;
            return nullptr;
        };
        const auto save=[&] {window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto revision=[&]() -> std::int64_t {
            for(const auto& item:run("documents"))if(item.value("active",false))return item.at("revision").get<std::int64_t>();
            throw std::runtime_error("No active clipboard test document");
        };
        select(opening);const auto before_copy=revision();key(tree,Qt::Key_C);
        check(revision()==before_copy,"Copy changed document revision");
        check(QApplication::clipboard()->mimeData()->hasFormat("application/x-zima-opening-settings-token"),"Tree Ctrl+C did not copy Opening settings");
        key(view,Qt::Key_V);auto* pending=dialog();check(pending,"Ctrl+V did not open Opening creation");
        check(revision()==before_copy,"Paste preview changed document revision");
        check(pending->pending_value().thread==source.thread&&pending->pending_value().placement.x!=source.placement.x,"Pasted Opening lost settings or copied placement");
        pending->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(!dialog()&&revision()==before_copy&&save().history.size()==initial.history.size(),"Cancel inserted an Opening or transaction");
        std::set<std::string> ids{source.id};
        for(double x:{0.,15.}) {
            // The second copy comes from the ordinary View selection route.
            if(x==15.){view->confirm_container(opening);flush();key(view,Qt::Key_C);}
            key(view,Qt::Key_V);pending=dialog();check(pending,"Repeated Ctrl+V did not open creation");
            const auto draft=pending->pending_value();check(ids.insert(draft.id).second&&draft.hole.sketch_id!=source.hole.sketch_id,"Paste reused an Opening or Sketch ID");
            check(draft.thread==source.thread&&draft.hole.drill_point_enabled==source.hole.drill_point_enabled&&draft.hole.drill_point_angle_degrees==source.hole.drill_point_angle_degrees,"Paste lost thread/chamfer/tip settings");
            if(x==15.) {
                auto* table=pending->findChild<QTableWidget*>("primitiveReferenceTable");check(table,"Pasted Opening reference table missing");
                const auto field=table->visualItemRect(table->item(0,1)).center();
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(field),QPointF(table->viewport()->mapToGlobal(field)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(field),QPointF(table->viewport()->mapToGlobal(field)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(table->viewport(),&press);QApplication::sendEvent(table->viewport(),&release);flush();
                QTreeWidgetItem* origin=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)
                    if((*i)->data(0,Qt::UserRole).toString().toStdString()==initial.body_history.active_body_id()+":origin"&&(*i)->data(0,Qt::UserRole+3)=="document-origin"){origin=*i;break;}
                check(origin,"Body Origin row missing");tree->scrollToItem(origin);flush();const auto point=tree->visualItemRect(origin).center();
                QMouseEvent pick(QEvent::MouseButtonPress,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent done(QEvent::MouseButtonRelease,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(),&pick);QApplication::sendEvent(tree->viewport(),&done);flush();
                const auto references=pending->pending_value().placement.references;
                check(references.size()>=3,"Pasted Opening whole-Origin entry failed");
                table=pending->findChild<QTableWidget*>("primitiveReferenceTable");bool offset=false;
                for(std::size_t i=0;i<std::min<std::size_t>(3,references.size());++i)if(references[i].semantic_key=="origin:plane:yz") {
                    auto* input=qobject_cast<QDoubleSpinBox*>(table->cellWidget(static_cast<int>(i),3));check(input&&input->isEnabled(),"Origin X offset input missing");input->setValue(x);offset=true;break;
                }
                check(offset,"Origin X plane missing");flush();
            } else {
                const auto position=pending->findChildren<QDoubleSpinBox*>("primitiveTranslation");check(position.size()==3,"Paste lacks normal placement controls");position.front()->setValue(x);flush();
            }
            pending->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Opening paste OK failed");
            auto saved=save();check(saved.find_container(draft.id)&&*saved.find_container(opening)==source,"Paste changed source or lost clone");
            if(x==15.)check(saved.find_container(draft.id)->placement.reference_valid&&saved.find_container(draft.id)->placement.references.size()>=3,"Pasted whole-Origin references did not persist");
            if(x==15.) {
                const auto before_inspection=revision();
                select(draft.id);auto* row=tree->currentItem();tree->scrollToItem(row);flush();
                const auto point=tree->visualItemRect(row).center();
                QMouseEvent double_click(QEvent::MouseButtonDblClick,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(),&double_click);flush();
                check(!dialog(),"Ordinary Opening inspection opened Properties");
                int axial_dimensions=0,diameter_dimensions=0;
                for(const auto& dimension:view->mesh().dimensions)if(dimension.reference.owner_id==draft.id) {
                    if(dimension.kind==kernel::ViewerDimensionKind::Diameter) {
                        check(std::abs(dimension.plane_normal.x)<1e-8&&std::abs(dimension.plane_normal.y)<1e-8&&std::abs(dimension.plane_normal.z-1)<1e-8,"Opening inspection diameter plane differs from its bore");++diameter_dimensions;
                    }
                    if(dimension.reference.semantic_key=="parameter:bore_length"||dimension.reference.semantic_key=="parameter:thread_length") {
                        const auto a=dimension.witness_first,b=dimension.witness_second;
                        check(std::abs(b.x-a.x)<1e-8&&std::abs(b.y-a.y)<1e-8&&std::abs(b.z-a.z-dimension.value)<1e-8,"Opening inspection length differs from its bore axis");++axial_dimensions;
                    }
                }
                check(axial_dimensions==2&&diameter_dimensions==2,"Whole-Origin Opening inspection dimensions missing");
                check(revision()==before_inspection,"Opening inspection changed the document revision");
            }
            window.findChild<QAction*>("undoAction")->trigger();flush();check(!save().find_container(draft.id),"Opening paste Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(save().find_container(draft.id),"Opening paste Redo lost identity");
        }
        auto* text=new QLineEdit(view);text->setText("ordinary text");text->show();text->selectAll();key(text,Qt::Key_C);
        check(QApplication::clipboard()->text()=="ordinary text","Model shortcut stole text Copy");
        QApplication::clipboard()->setText("pasted text");text->clear();key(text,Qt::Key_V);check(text->text()=="pasted text","Model shortcut stole text Paste");
        text->hide();delete text;key(view,Qt::Key_V);check(!dialog(),"Text clipboard pasted stale Opening settings");
        select(opening);key(tree,Qt::Key_C);select(blank);key(tree,Qt::Key_C);key(view,Qt::Key_V);check(!dialog(),"Unsupported feature copied or pasted a stale Opening");
        run("close");run("open",{{"path",path.generic_string()}});const auto restored=save();
        check(restored.history.size()==initial.history.size()+2&&*restored.find_container(opening)==source,"Opening copy round trip changed history");
        std::vector<kernel::BodyResult> bodies;static_cast<void>(document::PartDocument::load(path,&bodies));check(!bodies.empty(),"Copied Opening has no calculated body");
        kernel::OcctKernel kernel;auto body=bodies.back();kernel.ensure_body_properties(body);
        const double r=source.thread.profile_diameter/2,c=source.thread.chamfer_depth;
        const double tip=r/std::tan(source.hole.drill_point_angle_degrees*std::numbers::pi/360);
        const double removed=std::numbers::pi*(r*r*source.thread.bore_length+r*r*tip/3+r*c*c+c*c*c/3);
        check(std::abs(body.volume-(216000-3*removed))<1e-5,"Copied Opening independent material volume");
        window.grab().save(QString::fromStdString((directory/"opening-copy-paste.png").string()));
        std::cout<<"Opening clipboard: Tree/View Ctrl+C, repeated Ctrl+V, settings, fresh IDs, Cancel, Undo/Redo, text shortcuts, unsupported selection, native reopen and independent volume passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
