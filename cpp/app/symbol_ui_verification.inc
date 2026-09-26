int verify_symbol_ui(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle_view=[&]{QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();};
    try {
        const auto prepare=[](auto& text){app::rebuild_sketch_text_contours(text,true);};
        const auto target=directory/"symbol-ui.tblz";
        drawing::save_template_sketch(drawing::create_template_sketch(true,"Symbols"),target);
        check(window.open_document_path(QString::fromStdString(target.string())),"Cannot open symbol fixture");window.show();flush();
        auto* insert=window.findChild<QAction*>("insertSymbolAction");auto* save=window.findChild<QAction*>("saveDocumentAction");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(insert&&insert->isEnabled()&&tree&&view&&save,"Symbol command unavailable");
        auto chosen_symbol=std::filesystem::current_path()/"config/symbols/drawing-conventions/ZE-PROJECTION-METHOD.symz";
        QTimer chooser;QObject::connect(&chooser,&QTimer::timeout,[&]{if(auto* file=window.findChild<QFileDialog*>()) {
            file->findChild<QLineEdit*>("fileNameEdit")->setText(QString::fromStdString(chosen_symbol.string()));
            QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);chooser.stop();
        }});
        for(bool accept:{false,true}) {
            chooser.start(100);insert->trigger();chooser.stop();flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");
            check(dialog,"Symbol insertion dialog missing");check((dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Symbol uses a native property dialog");
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(25);
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement1")->setValue(15);
            if(accept) {
                QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(view->mapToGlobal(QPoint(20,20))),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
                QApplication::sendEvent(view,&confirm);
            }else dialog->reject();flush();save->trigger();flush();
            check(drawing::load_template_sketch(target,prepare).symbols.size()==(accept?1:0),"Symbol OK/Cancel did not preserve ownership");
        }
        const auto original=drawing::load_template_sketch(target,prepare).symbols.front();
        const auto select=[&](){QTreeWidgetItemIterator it(tree);while(*it){auto* item=*it++;if(item->data(0,Qt::UserRole+3)=="sketch-symbol"){tree->setCurrentItem(item);return item;}}return static_cast<QTreeWidgetItem*>(nullptr);};
        auto* item=select();check(item,"Symbol Tree row missing");flush();
        check(view->confirmed_candidate()&&view->confirmed_candidate()->kind==viewer::CandidateKind::Symbol,"Tree did not select symbol in View");
        for(const QString language:{"cs","en","de","fr","ru"}) {
            auto settings=app::ApplicationSettings::load();settings.language=language;
            QSettings config(QString::fromStdString((directory/"symbol-language.ini").string()),QSettings::IniFormat);config.setValue("Application/Language",language);config.sync();
            QTemporaryDir translated;QFile::copy(config.fileName(),translated.filePath("config.ini"));settings=app::ApplicationSettings::load(translated.path());
            app::apply_application_translations(application,settings);
            window.show_tree_item_properties(select());flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");check(dialog,"Cannot edit symbol");
            check(dialog->findChild<QCheckBox*>("symbolCadVariant")->text()==settings.qt_translations.value("Podle nastavení výkresu"),"Symbol translation differs");
            dialog->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(99);dialog->reject();flush();save->trigger();flush();
            check(drawing::load_template_sketch(target,prepare).symbols.front()==original,"Symbol edit Cancel changed document");
            auto* text_options=new app::SketchTextPropertiesDialog(sketcher::Sketch::create_text(),std::array<double,2>{0,0},{},[](auto){},&window,true,false,std::nullopt,true);
            const auto* readable=text_options->findChild<QComboBox*>("symbolTextDrawingOrientation");
            check(readable&&readable->itemText(0)==settings.qt_translations.value("Se značkou")&&readable->itemText(1)==settings.qt_translations.value("Zachovat čitelnost"),"Symbol text orientation translation differs");
            text_options->reject();flush();
            symbols::Placement translated_symbol;translated_symbol.symbol=original;
            auto* attachment_options=new app::SymbolAttachmentDialog(translated_symbol,[](auto){},&window,true);
            check(attachment_options->findChild<QCheckBox*>("symbolPerpendicularLeader")->text()==settings.qt_translations.value("Odkaz kolmý k entitě"),"Perpendicular leader translation differs");
            const auto* shelf=attachment_options->findChild<QComboBox*>("symbolShelfMode");
            check(shelf&&shelf->itemText(0)==settings.qt_translations.value("Pod symbolem")&&shelf->itemText(1)==settings.qt_translations.value("K uchopovacímu bodu"),"Shelf choices are untranslated");
            attachment_options->reject();flush();
            const auto weld_definition=symbols::Definition::load(std::filesystem::current_path()/"config/symbols/welding/ZE-FILLET-WELD-ISO2553.symz");
            sketcher::SymbolInstance weld;weld.id="localized-weld";weld.definition=weld_definition.serialized();weld.variant=weld_definition.default_variant;
            auto* weld_options=new app::SymbolDialog(weld,{},[](auto){},&window);
            auto* variant=weld_options->findChild<QComboBox*>("symbolVariant");
            check(variant->itemText(variant->findData("arrow_side"))==settings.qt_translations.value("ISO 2553 A — strana šipky"),"Weld arrow-side label is untranslated");
            check(variant->itemText(variant->findData("other_side"))==settings.qt_translations.value("ISO 2553 A — opačná strana"),"Weld other-side label is untranslated");
            weld_options->reject();flush();

        }
        app::apply_application_translations(application,app::ApplicationSettings::load());
        window.show_tree_item_properties(select());flush();auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");
        dialog->findChild<QCheckBox*>("symbolCadVariant")->setChecked(false);auto* variant=dialog->findChild<QComboBox*>("symbolVariant");variant->setCurrentIndex(variant->findData("third_angle"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
        const auto edited=drawing::load_template_sketch(target,prepare).symbols.front();check(edited.variant=="third_angle"&&!edited.use_cad_variant,"Symbol variant did not persist");
        view->clear_selection();view->fit_all();flush();
        const auto rendered=view->grabFramebuffer();
        rendered.save(QString::fromStdString((directory/"symbol-framebuffer.png").string()));
        std::size_t symbol_pixels=0;
        for(int y=0;y<rendered.height();++y)for(int x=0;x<rendered.width();++x) {
            const auto color=rendered.pixelColor(x,y);
            // The current projection definition uses the green plotting pen;
            // orange Sketch axes cannot satisfy this geometry check.
            if(color.green()>150&&color.green()>color.red()*1.5&&color.green()>color.blue()*1.5)++symbol_pixels;
        }
        check(symbol_pixels>100,"Symbol geometry is stored but not painted in Sketcher");
        window.grab().save(QString::fromStdString((directory/"symbol-editor.png").string()));
        const auto definition=symbols::projection_method();const auto editable=directory/"editable-symbol.symz";
        definition.save(editable);
        check(window.open_document_path(QString::fromStdString(editable.string())),"Cannot open native symbol document");flush();
        check(!insert->isEnabled(),"Symbol editor offers recursive symbol insertion");
        auto* choice=window.findChild<QComboBox*>("symbolSketchChoice");
        check(choice&&choice->count()==2,"Symbol editor lost variant sketches");
        choice->setCurrentIndex(1);QMetaObject::invokeMethod(choice,"activated",Qt::DirectConnection,Q_ARG(int,1));flush();
        save->trigger();flush();
        check(symbols::Definition::load(editable).serialized()==definition.serialized(),"GUI Save changed variant metadata or native format");
        auto* create=window.findChild<QAction*>("newDocumentAction");check(create,"New document action unavailable");create->trigger();flush();
        auto* fresh=window.findChild<QDialog*>("newDocumentDialog");check(fresh,"New document dialog unavailable");
        bool offered=false;for(auto* radio:fresh->findChildren<QRadioButton*>())if(radio->property("documentType")=="symbol") {radio->setChecked(true);offered=true;}
        const auto fresh_name=QString("NEW-SYMBOL-%1").arg(QString::fromStdString(document::PartDocument::create_default().document_id)).toUpper();
        check(offered,"New Symbol document not offered");fresh->findChild<QLineEdit*>("newDocumentFileName")->setText(fresh_name);
        fresh->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("newDocumentDialog"),"New Symbol failed");save->trigger();flush();
        const auto fresh_path=directory/(fresh_name.toStdString()+".symz");
        check(std::filesystem::exists(fresh_path),"New Symbol GUI Save did not write SYMZ");
        check(symbols::Definition::load(fresh_path).insertion_point==std::array<double,2>{0,0},"New Symbol grip is not the origin");
        auto* text_action=window.findChild<QAction*>("sketchTextAction");check(text_action&&text_action->isEnabled(),"Symbol text command unavailable");
        for(bool accept:{false,true}) {
            text_action->trigger();flush();
            auto* text_dialog=dynamic_cast<app::SketchTextPropertiesDialog*>(window.findChild<QDialog*>("sketchTextProperties"));
            check(text_dialog,"Symbol text editor missing");text_dialog->set_anchor(2,1);
            auto* orientation=text_dialog->findChild<QComboBox*>("symbolTextDrawingOrientation");
            check(orientation&&orientation->count()==2&&!orientation->currentData().toBool(),"Symbol text orientation option missing or enabled by default");
            orientation->setCurrentIndex(1);
            text_dialog->findChild<QPlainTextEdit*>("sketchTextValue")->setPlainText("Ra 3.2");
            check(!text_dialog->findChild<QCheckBox*>("sketchTextFlipped")->isChecked(),"New symbol text is reflected");
            if(accept)text_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else text_dialog->reject();
            flush();save->trigger();flush();
            const auto authored=symbols::Definition::load(fresh_path);
            check(authored.sketches.front().texts.size()==(accept?1:0),"Symbol text OK/Cancel failed");
            if(accept) {
                const auto& t=authored.sketches.front().texts.front();
                check(t.drawing_keep_readable&&t.value=="Ra 3.2"&&!t.modeling_geometry&&t.height==2.5&&!t.flipped,"Symbol text properties lost");
                auto rebuilt=t;app::rebuild_sketch_text_contours(rebuilt,true);
                check(rebuilt.contours==t.contours,"Symbol text orientation differs after evaluation/reopen");
                auto* reopened_text=new app::SketchTextPropertiesDialog(t,std::array<double,2>{t.anchor_x,t.anchor_y},{},[](auto){},&window,true,false,std::nullopt,true);
                auto* reopened_orientation=reopened_text->findChild<QComboBox*>("symbolTextDrawingOrientation");
                check(reopened_orientation&&reopened_orientation->currentData().toBool(),"Reopened text lost readable setting");
                reopened_orientation->setCurrentIndex(0);reopened_text->reject();flush();save->trigger();flush();
                check(symbols::Definition::load(fresh_path).sketches.front().texts.front().drawing_keep_readable,"Cancel changed saved text readability");
            }
        }
        view->fit_all();flush();
        window.grab().save(QString::fromStdString((directory/"symbol-authoring.png").string()));
        auto model=document::PartDocument::create_default();auto cylinder=test::circular_feature(model,8,25);model.history.push_back(cylinder);model.insert_history_entry(document::PartHistoryKind::Feature,cylinder.id);
        kernel::OcctKernel kernel;const auto model_path=directory/"symbol-attachment.prtz";model.save(model_path,kernel.evaluate_history(model.kernel_operations()));
        check(window.open_document_path(QString::fromStdString(model_path.string())),"Cannot open model symbol fixture");flush();
        check(insert->isEnabled(),"Model symbol insertion unavailable");view->set_view_direction({1,-1,.5});view->fit_all();flush();
        for(bool accept:{false,true}) {
            chooser.start(100);insert->trigger();chooser.stop();flush();
            auto* attachment=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));check(attachment,"Model symbol attachment dialog missing");
            check((attachment->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Attachment uses native dialog");
            settle_view();
            std::optional<QPointF> hit;
            for(int y=8;y<view->height()&&!hit;y+=8)for(int x=8;x<view->width();x+=8){const QPointF p(x,y);if(attachment->geometry().contains(view->mapTo(&window,p.toPoint())))continue;const auto offered=view->selection_candidates_at(p);
                if(!offered.empty()&&offered.front().kind==viewer::CandidateKind::Face&&offered.front().owner_id==cylinder.id){hit=p;break;}}
            check(hit.has_value(),"Common picker did not offer symbol attachment face");const auto p=*hit;
            QCursor::setPos(view->mapToGlobal(p.toPoint()));flush();
            QMouseEvent move(QEvent::MouseMove,p,QPointF(view->mapToGlobal(p.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
            QMouseEvent press(QEvent::MouseButtonPress,p,QPointF(view->mapToGlobal(p.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,p,QPointF(view->mapToGlobal(p.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
            if(!attachment->pending_placement().reference){window.grab().save(QString::fromStdString((directory/"symbol-pick-failure.png").string()));
                const auto offered=view->offered_candidate();std::cerr<<"Pick: "<<p.x()<<","<<p.y()<<" active="<<attachment->active()<<" offered="<<(offered?offered->semantic_key:"none")<<"\n";}
            check(attachment->pending_placement().reference.has_value(),"Model face click did not attach symbol");
            check(!attachment->active(),"Reference input remained armed after symbol attachment");
            attachment->findChild<QCheckBox*>("symbolLeader")->setChecked(true);attachment->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(15);
            if(accept)attachment->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else attachment->reject();flush();save->trigger();flush();
            check(document::PartDocument::load(model_path).symbol_annotations.size()==(accept?1:0),"Model symbol OK/Cancel failed");
        }
        QTreeWidgetItem* model_symbol=nullptr;QTreeWidgetItemIterator rows(tree);while(*rows){auto* row=*rows++;if(row->data(0,Qt::UserRole+3)=="model-symbol"){model_symbol=row;break;}}
        check(model_symbol,"Model symbol Tree row missing");tree->setCurrentItem(model_symbol);flush();
        check(view->confirmed_candidate()&&view->confirmed_candidate()->kind==viewer::CandidateKind::Symbol,"Model Tree/View symbol selection failed");
        const auto contact_handle=view->symbol_handle_position(0),grip_handle=view->symbol_handle_position(1);
        check(contact_handle&&grip_handle&&view->symbol_handle_position(2),"Selected model leader does not expose three handles");
        std::optional<QPointF> symbol_hit;
        for(int y=-12;y<=12&&!symbol_hit;y+=2)for(int x=-12;x<=12;++x){const auto p=*grip_handle+QPointF(x,y);
            const auto candidates=view->selection_candidates_at(p);if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Symbol){symbol_hit=p;break;}}
        check(symbol_hit.has_value(),"Ordinary Part hover does not offer symbol");
        auto* symbols_filter=window.findChild<QAction*>("showSymbolsAction");check(symbols_filter,"Symbols display filter missing");
        symbols_filter->setChecked(false);flush();
        check(!view->symbol_handle_position(0),"Hidden symbol retained editing handle");
        for(const auto& c:view->selection_candidates_at(*symbol_hit))check(c.kind!=viewer::CandidateKind::Symbol,"Hidden symbol is still pickable");
        symbols_filter->setChecked(true);view->set_reference_visibility(viewer::ReferenceVisibility::Sketches,false);view->set_reference_visibility(viewer::ReferenceVisibility::Dimensions,false);flush();
        const auto symbol_candidates=view->selection_candidates_at(*symbol_hit);
        check(std::ranges::any_of(symbol_candidates,[](const auto& c){return c.kind==viewer::CandidateKind::Symbol;}),"Symbols depend on Sketch/Dimension display filters");
        view->set_reference_visibility(viewer::ReferenceVisibility::Sketches,true);view->set_reference_visibility(viewer::ReferenceVisibility::Dimensions,true);
        view->confirm_reference(model_symbol->data(0,Qt::UserRole).toString().toStdString(),"symbol:"+model_symbol->data(0,Qt::UserRole).toString().toStdString(),{},viewer::CandidateKind::Symbol);
        const auto stored=document::PartDocument::load(model_path).symbol_annotations.front();
        window.show_tree_item_properties(model_symbol);flush();auto* attachment=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));
        check(attachment&&attachment->pending_placement()==stored,"Model symbol reopen lost contact or text values");
        const auto drag_grip=view->symbol_handle_position(1);check(drag_grip.has_value(),"Editing symbol grip missing");
        const auto send_handle=[&](QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons){
            QMouseEvent event(type,pos,QPointF(view->mapToGlobal(pos.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        send_handle(QEvent::MouseButtonPress,*drag_grip,Qt::LeftButton,Qt::LeftButton);
        send_handle(QEvent::MouseMove,*drag_grip+QPointF(35,20),Qt::NoButton,Qt::LeftButton);
        send_handle(QEvent::MouseButtonRelease,*drag_grip+QPointF(35,20),Qt::LeftButton,Qt::NoButton);
        check(attachment->pending_placement().symbol.x!=stored.symbol.x||attachment->pending_placement().symbol.y!=stored.symbol.y,"Symbol grip drag did not move preview");
        check(attachment->pending_placement().reference==stored.reference,"Grip drag replaced contact reference");
        auto* shelf_mode=attachment->findChild<QComboBox*>("symbolShelfMode");auto* shelf_length=attachment->findChild<QDoubleSpinBox*>("symbolShelfLength");
        check(shelf_mode&&shelf_length,"Shelf controls missing");shelf_mode->setCurrentIndex(1);shelf_length->setValue(3);flush();
        const auto shelf_end=view->symbol_handle_position(2);check(shelf_end.has_value(),"Short shelf end missing");
        const auto previous_length=attachment->pending_placement().shelf_length;
        send_handle(QEvent::MouseButtonPress,*shelf_end,Qt::LeftButton,Qt::LeftButton);
        send_handle(QEvent::MouseMove,*shelf_end+QPointF(28,0),Qt::NoButton,Qt::LeftButton);
        send_handle(QEvent::MouseButtonRelease,*shelf_end+QPointF(28,0),Qt::LeftButton,Qt::NoButton);
        check(std::abs(attachment->pending_placement().shelf_length-previous_length)>.01,"Short shelf drag did not change length");
        check(attachment->pending_placement().reference==stored.reference,"Shelf resizing replaced reference");
        const auto edit_contact=view->symbol_handle_position(0);check(edit_contact.has_value(),"Editing contact missing");
        send_handle(QEvent::MouseButtonPress,*edit_contact,Qt::LeftButton,Qt::LeftButton);
        send_handle(QEvent::MouseButtonRelease,*edit_contact,Qt::LeftButton,Qt::NoButton);
        check(attachment->active(),"Contact handle did not arm reference replacement");
        attachment->reject();flush();save->trigger();flush();
        check(document::PartDocument::load(model_path).symbol_annotations.front()==stored,"Cancel committed handle preview");
        view->clear_selection();view->fit_all();flush();window.grab().save(QString::fromStdString((directory/"symbol-attachment.png").string()));
        auto assembly_model=assembly::AssemblyDocument::create_default();assembly::PartOccurrence occurrence;
        occurrence.occurrence_id="symbol-ui-occurrence";occurrence.name="symbol-attachment.prtz";occurrence.source_document_id=model.document_id;occurrence.source_path=model_path;
        occurrence.calculated_source=document::component_source(model,kernel.evaluate_history(model.kernel_operations()));assembly_model.components.push_back(occurrence);
        const auto assembly_path=directory/"symbol-assembly.asmz";assembly_model.save(assembly_path);
        QTimer error_guard;QObject::connect(&error_guard,&QTimer::timeout,[&] {
            for(auto* widget:QApplication::topLevelWidgets())if(auto* message=qobject_cast<QMessageBox*>(widget);message&&message->isVisible()) {
                std::cerr<<"Unexpected Assembly fixture dialog: "<<message->text().toStdString()<<"\n";message->reject();
            }
        });error_guard.start(100);
        check(window.open_document_path(QString::fromStdString(assembly_path.string())),"Cannot open Assembly symbol fixture");error_guard.stop();flush();view->set_view_direction({1,-1,.5});view->fit_all();flush();
        chooser.start(100);insert->trigger();chooser.stop();flush();
        auto* assembly_symbol=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));check(assembly_symbol,"Assembly symbol dialog missing");settle_view();
        std::optional<QPointF> assembly_hit;
        for(int y=8;y<view->height()&&!assembly_hit;y+=8)for(int x=8;x<view->width();x+=8){const QPointF p(x,y);if(assembly_symbol->geometry().contains(view->mapTo(&window,p.toPoint())))continue;const auto offered=view->selection_candidates_at(p);
            if(!offered.empty()&&offered.front().kind==viewer::CandidateKind::Face&&offered.front().owner_id==cylinder.id){assembly_hit=p;break;}}
        check(assembly_hit.has_value(),"Assembly face not offered for symbol");const auto ap=*assembly_hit;QCursor::setPos(view->mapToGlobal(ap.toPoint()));flush();
        QMouseEvent assembly_move(QEvent::MouseMove,ap,QPointF(view->mapToGlobal(ap.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&assembly_move);
        QMouseEvent assembly_press(QEvent::MouseButtonPress,ap,QPointF(view->mapToGlobal(ap.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&assembly_press);
        QMouseEvent assembly_release(QEvent::MouseButtonRelease,ap,QPointF(view->mapToGlobal(ap.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&assembly_release);flush();
        check(assembly_symbol->pending_placement().reference&&assembly_symbol->pending_placement().reference->document_id==model.document_id&&!assembly_symbol->pending_placement().reference->instance_path.empty(),"Assembly symbol lost source document or occurrence identity");
        assembly_symbol->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
        check(assembly::AssemblyDocument::load(assembly_path).symbol_annotations.size()==1,"Assembly symbol did not persist");
        auto sheet_doc=drawing::DrawingDocument::create_default();
        kernel::ViewerMesh line_mesh;kernel::ViewerEdge line;line.reference={cylinder.id,"symbol-test-edge",{}};line.points={{-30,0,0},{30,0,0}};line_mesh.edges.push_back(line);
        auto drawing_view=drawing::DrawingDocument::create_view(model.document_id,model_path,line_mesh);
        drawing_view.camera={{1,0,0},{0,1,0},{0,0,1}};drawing_view.x=100;drawing_view.y=180;drawing_view.scale=1;
        drawing::capture_measurement_geometry(drawing_view,line_mesh);sheet_doc.sheets.front().views={drawing_view};
        const auto drawing_path=directory/"symbol-sheet.drwz";sheet_doc.save(drawing_path);
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot open Drawing symbol fixture");flush();
        auto* drawing_window=dynamic_cast<app::DrawingWindow*>(window.findChild<QWidget*>("drawingWorkspace"));
        auto* canvas=drawing_window?drawing_window->findChild<QWidget*>("drawingCanvas"):nullptr;
        auto* insert_sheet=drawing_window?drawing_window->findChild<QAction*>("drawingSymbolAction"):nullptr;
        check(drawing_window&&canvas&&insert_sheet&&insert_sheet->isEnabled(),"Drawing symbol command unavailable");
        chosen_symbol=std::filesystem::current_path()/"config/symbols/surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz";
        for(bool accept:{false,true}) {
            chooser.start(100);insert_sheet->trigger();chooser.stop();flush();
            auto* editor=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));
            check(editor,"Drawing symbol dialog missing");
            const auto rect=drawing_window->sheet_rectangle_for_test();const auto& sheet=drawing_window->document_for_test().sheets.front();
            const double zoom=rect.width()/sheet.width_mm();
            const QPointF point=rect.topLeft()+QPointF(sheet.width_mm()-100,sheet.height_mm()-180)*zoom;
            QMouseEvent move(QEvent::MouseMove,point,QPointF(canvas->mapToGlobal(point.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&move);
            QMouseEvent press(QEvent::MouseButtonPress,point,QPointF(canvas->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,QPointF(canvas->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&release);flush();
            check(editor->pending_placement().reference.has_value(),"Drawing common picker did not attach symbol");
            editor->findChild<QCheckBox*>("symbolLeader")->setChecked(true);editor->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(20);
            if(accept)editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else editor->reject();flush();
            check(drawing_window->document_for_test().sheets.front().symbol_annotations.size()==(accept?1:0),"Drawing symbol Cancel/OK failed");
        }
        save->trigger();flush();check(drawing::DrawingDocument::load(drawing_path).sheets.front().symbol_contacts.size()==1,"Drawing symbol reference did not persist");
        const auto sheet_symbol=drawing_window->document_for_test().sheets.front().symbol_annotations.front();
        const auto sheet_key="drawing-symbol:"+sheet_symbol.symbol.id;drawing_window->select_tree_entities({sheet_key},sheet_key);flush();
        QMenu sheet_menu;drawing_window->populate_selection_menu(sheet_menu);auto* sheet_properties=sheet_menu.findChild<QAction*>("drawingEntityPropertiesAction");
        check(sheet_properties,"Drawing symbol Properties action missing");sheet_properties->trigger();flush();
        auto* reopened_symbol=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));
        check(reopened_symbol&&reopened_symbol->pending_placement()==sheet_symbol,"Drawing symbol Properties lost reference or local pose");
        {
            reopened_symbol->findChild<QCheckBox*>("symbolPerpendicularLeader")->setChecked(true);flush();
            auto value=reopened_symbol->pending_placement();
            check(value.paper_tangent.has_value(),"Straight Drawing symbol has no contact tangent");
            const auto mesh=value.viewer_mesh(0.,true);
            const auto stroke=std::ranges::find_if(mesh.edges,[](const auto& edge){return edge.annotation&&edge.annotation->role==1;});
            check(stroke!=mesh.edges.end()&&stroke->points.size()==2,"Perpendicular Drawing leader is missing");
            const auto a=stroke->points.front(),b=stroke->points.back(),t=*value.paper_tangent;
            check(std::abs((b.x-a.x)*t.x+(b.y-a.y)*t.y)<1e-8,"Drawing leader is not perpendicular after checking the option");
            const auto rect=drawing_window->sheet_rectangle_for_test();const auto& page=drawing_window->document_for_test().sheets.front();const double zoom=rect.width()/page.width_mm();
            const auto start=rect.topLeft()+QPointF(page.width_mm()-value.frame.origin.x,page.height_mm()-value.frame.origin.y)*zoom;
            const auto delta=QPointF(-t.x,-t.y)*10*zoom;
            const auto send=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,p,QPointF(canvas->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&event);flush();};
            send(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);send(QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton);
            const auto moved=reopened_symbol->pending_placement();
            check(moved.reference==value.reference&&std::hypot(moved.frame.origin.x-value.frame.origin.x,moved.frame.origin.y-value.frame.origin.y)>9.9,"Drawing arrow handle cannot slide on its original entity");
            check(drawing_window->document_for_test().sheets.front().symbol_annotations.front()==sheet_symbol,"Dragging a pending symbol committed before OK");
            const auto second_start=rect.topLeft()+QPointF(page.width_mm()-moved.frame.origin.x,page.height_mm()-moved.frame.origin.y)*zoom;
            send(QEvent::MouseButtonPress,second_start,Qt::LeftButton,Qt::LeftButton);
            send(QEvent::MouseMove,second_start+delta,Qt::NoButton,Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&escape);flush();
            check(reopened_symbol->isVisible()&&reopened_symbol->pending_placement()==moved,"Escape did not restore the pending arrow drag");
            reopened_symbol->findChild<QCheckBox*>("symbolPerpendicularLeader")->setChecked(false);flush();
        }
        reopened_symbol->findChild<QComboBox*>("symbolShelfMode")->setCurrentIndex(1);reopened_symbol->findChild<QDoubleSpinBox*>("symbolShelfLength")->setValue(8);flush();
        {
            const auto value=reopened_symbol->pending_placement();const auto end=value.frame.world({value.symbol.x,value.symbol.y,0});
            const auto rect=drawing_window->sheet_rectangle_for_test();const auto& page=drawing_window->document_for_test().sheets.front();const double zoom=rect.width()/page.width_mm();
            const auto start=rect.topLeft()+QPointF(page.width_mm()-end.x,page.height_mm()-end.y)*zoom;
            const auto send=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,p,QPointF(canvas->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&event);flush();};
            send(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);send(QEvent::MouseMove,start+QPointF(24,0),Qt::NoButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,start+QPointF(24,0),Qt::LeftButton,Qt::NoButton);
            check(std::abs(reopened_symbol->pending_placement().shelf_length-8)>.01,"Drawing shelf end drag did not resize preview");
            check(reopened_symbol->pending_placement().reference==value.reference,"Drawing shelf drag replaced attachment");
        }
        reopened_symbol->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(99);reopened_symbol->reject();flush();
        check(drawing_window->document_for_test().sheets.front().symbol_annotations.front()==sheet_symbol,"Drawing symbol edit Cancel changed stored annotation");
        {
            const auto& page=drawing_window->document_for_test().sheets.front();
            const auto view_id=page.symbol_contacts.at(sheet_symbol.symbol.id).view_id;
            drawing_window->select_view_for_test(view_id);flush();
            const auto center=drawing_window->view_rectangle_center_for_test(view_id);check(center.has_value(),"Symbol source view rectangle missing");
            const auto before_view=*std::ranges::find(page.views,view_id,&drawing::DrawingView::id);
            const auto before=page.symbol_annotations.front().viewer_mesh(0.);
            const auto send=[&](QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,pos,QPointF(canvas->mapToGlobal(pos.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);flush();};
            // The center is occupied by the attached arrow; grab empty space in the view rectangle.
            const auto grab=*center+QPointF(-25,0);
            send(QEvent::MouseButtonPress,grab,Qt::LeftButton,Qt::LeftButton);
            send(QEvent::MouseMove,grab+QPointF(35,22),Qt::NoButton,Qt::LeftButton);
            send(QEvent::MouseButtonRelease,grab+QPointF(35,22),Qt::LeftButton,Qt::NoButton);
            const auto& moved_page=drawing_window->document_for_test().sheets.front();
            const auto after_view=*std::ranges::find(moved_page.views,view_id,&drawing::DrawingView::id);
            const double dx=after_view.x-before_view.x,dy=after_view.y-before_view.y;
            check(std::hypot(dx,dy)>1,"View rectangle GUI drag did not move");
            const auto after=moved_page.symbol_annotations.front().viewer_mesh(0.);
            for(std::size_t e=0;e<before.edges.size();++e)for(std::size_t i=0;i<before.edges[e].points.size();++i){
                const auto a=before.edges[e].points[i],b=after.edges[e].points[i];
                check(std::abs(b.x-a.x-dx)<1e-7&&std::abs(b.y-a.y-dy)<1e-7,"GUI view drag detached a symbol stroke");
            }
        }
        chosen_symbol=std::filesystem::current_path()/"config/symbols/geometric-tolerances/ZE-PERPENDICULARITY-ISO1101.symz";
        chooser.start(100);insert_sheet->trigger();chooser.stop();flush();
        auto* free_symbol=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));check(free_symbol,"Free Drawing symbol dialog missing");
        free_symbol->findChild<QDoubleSpinBox*>("symbolOrigin0")->setValue(90);free_symbol->findChild<QDoubleSpinBox*>("symbolOrigin1")->setValue(140);
        free_symbol->findChild<QComboBox*>("symbolField:Secondary datum")->setCurrentText("B");
        free_symbol->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(drawing_window->document_for_test().sheets.front().symbol_annotations.size()==2,"Free Drawing symbol was not committed");
        save->trigger();flush();
        drawing_window->render_sheet_for_test(false).save(QString::fromStdString((directory/"symbol-sheet.png").string()));
        drawing_window->export_pdf(directory/"symbol-sheet.pdf");drawing_window->export_dxf(directory/"symbol-sheet.dxf");
        check(std::filesystem::file_size(directory/"symbol-sheet.pdf")>1000&&std::filesystem::file_size(directory/"symbol-sheet.dxf")>1000,"Symbol export missing");
        auto imported_symbols=sketcher::Sketch::create_default();const auto dxf_check=interchange::import_dxf(directory/"symbol-sheet.dxf",imported_symbols);
        check(dxf_check.imported_entities>10,"Exported symbol DXF has no readable vector geometry");
        const auto tolerance_definition=symbols::Definition::load(std::filesystem::current_path()/"config/symbols/geometric-tolerances/ZE-PERPENDICULARITY-ISO1101.symz");
        sketcher::SymbolInstance tolerance;tolerance.id="tolerance-ui";tolerance.definition=tolerance_definition.serialized();tolerance.variant=tolerance_definition.default_variant;
        std::optional<sketcher::SymbolInstance> accepted_tolerance;
        auto* tolerance_dialog=new app::SymbolDialog(tolerance,{},[&](auto value){accepted_tolerance=std::move(value);},&window);tolerance_dialog->show();flush();
        const auto tolerance_field=[&](const char* name){return tolerance_dialog->findChild<QComboBox*>(QString("symbolField:")+name);};
        const auto submit_tolerance=[&]{tolerance_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();};
        tolerance_field("Tolerance")->setCurrentText("-0.1");submit_tolerance();check(!accepted_tolerance,"Negative geometric tolerance accepted");
        tolerance_field("Tolerance")->setCurrentText("0,05");tolerance_field("Primary datum")->setCurrentText("");submit_tolerance();check(!accepted_tolerance,"Orientation tolerance accepted without datum");
        tolerance_field("Primary datum")->setCurrentText("a");tolerance_field("Tertiary datum")->setCurrentText("C");submit_tolerance();check(!accepted_tolerance,"Gap in datum precedence accepted");
        tolerance_field("Secondary datum")->setCurrentText("B");submit_tolerance();check(accepted_tolerance&&accepted_tolerance->text_values.at("Tolerance")=="0.05"&&accepted_tolerance->text_values.at("Primary datum")=="A","Valid geometric tolerance was not normalized and committed");
        chosen_symbol=std::filesystem::current_path()/"config/symbols/welding/ZE-FILLET-WELD-ISO2553.symz";
        window.resize(1024,680);flush();
        for(const double offset:{-30.,30.}) {
            const auto before=drawing_window->document_for_test().sheets.front().symbol_annotations.size();
            for(bool accept:{false,true}) {
                chooser.start(100);insert_sheet->trigger();chooser.stop();flush();
                auto* editor=dynamic_cast<app::SymbolAttachmentDialog*>(window.findChild<QDialog*>("symbolAttachmentDialog"));check(editor,"Weld editor missing");
                check(editor->findChild<QCheckBox*>("symbolLeader")->isChecked(),"Weld did not default to a leader");
                auto* variant=editor->findChild<QComboBox*>("symbolVariant");variant->setCurrentIndex(variant->findData(offset<0?"other_side":"arrow_side"));
                const auto prefix=offset<0?"Other":"Arrow";
                editor->findChild<QDoubleSpinBox*>("symbolOrigin0")->setValue(140);editor->findChild<QDoubleSpinBox*>("symbolOrigin1")->setValue(offset<0?110:80);
                editor->findChild<QDoubleSpinBox*>("symbolPlacement0")->setValue(offset);editor->findChild<QDoubleSpinBox*>("symbolPlacement1")->setValue(12);
                editor->findChild<QComboBox*>(QString("symbolField:%1 size").arg(prefix))->setCurrentText("a4");
                editor->findChild<QComboBox*>(QString("symbolField:%1 length").arg(prefix))->setCurrentText("3 x 50 (100)");
                auto* ok=editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
                check(window.rect().contains(QRect(ok->mapTo(&window,QPoint()),ok->size())),"Weld OK button is outside laptop window");
                if(accept)ok->click();else editor->reject();flush();
                check(drawing_window->document_for_test().sheets.front().symbol_annotations.size()==before+(accept?1:0),"Weld OK/Cancel differs");
            }
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(drawing_window->document_for_test().sheets.front().symbol_annotations.size()==before,"Weld Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(drawing_window->document_for_test().sheets.front().symbol_annotations.size()==before+1,"Weld Redo failed");
        }
        save->trigger();flush();
        const auto reopened=drawing::DrawingDocument::load(drawing_path);
        check(reopened.sheets.front().symbol_annotations==drawing_window->document_for_test().sheets.front().symbol_annotations,"Weld values lost on save/reopen");
        drawing_window->render_sheet_for_test(false).save(QString::fromStdString((directory/"weld-tolerance-sheet.png").string()));
        drawing_window->export_pdf(directory/"weld-tolerance-sheet.pdf");drawing_window->export_dxf(directory/"weld-tolerance-sheet.dxf");
        auto exported=sketcher::Sketch::create_default();
        check(interchange::import_dxf(directory/"weld-tolerance-sheet.dxf",exported).imported_entities>30,"Weld DXF lacks vector geometry");
        std::cout<<"Symbol insertion, edit, cancellation, middle-button confirmation, Tree/View selection and five UI languages passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
