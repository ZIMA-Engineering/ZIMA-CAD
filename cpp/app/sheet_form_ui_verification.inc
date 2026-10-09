Q_NEVER_INLINE static int verify_corner_sheet_form_ui(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        struct CursorRestore {QPoint position=QCursor::pos();~CursorRestore(){QCursor::setPos(position);}} restore_cursor;
        const auto run=[&](const QString& command){const auto result=window.execute_console_command(command);check(result.ok,result.message.c_str());return result;};
        const auto preview_only=qEnvironmentVariableIsSet("ZIMA_CORNER_FORM_PREVIEW_ONLY");
        const auto dimensions_only=qEnvironmentVariableIsSet("ZIMA_CORNER_FORM_DIMENSIONS_ONLY");
        const auto custom_input=qEnvironmentVariable("ZIMA_CORNER_FORM_INPUT");
        const auto input=std::filesystem::absolute(custom_input.isEmpty()?
            (preview_only?"build/form-diagnostic/CornerBodyCutPreview.prtz":"build/form-diagnostic/CornerGusset90Placed.prtz"):custom_input.toStdString());
        std::vector<kernel::BodyResult> cache;const auto original=document::PartDocument::load(input,&cache);
        const auto path=directory/"corner-form-ui.prtz";original.save(path,cache);
        const auto id=original.history.back().id;
        check(preview_only||original.history.back().feature_kind==document::FeatureKind::SheetForm,"Missing corner FORM fixture");
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"Corner FORM could not open");flush();
        // Native hover events must not replace a synthetic View probe. Finish
        // camera transitions before fixing the probe's frame, as in other GUI
        // contracts; a moving camera changes the face underneath a test click.
        QCursor::setPos(window.mapToGlobal(QPoint(5,5)));flush();
        run(QString::fromStdString("body.activate "+original.body_history.bodies().front().scope.id));flush();
        auto* mode=window.findChild<QAction*>("applicationModeAction2");check(mode,"Sheet workspace missing");mode->trigger();flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Corner View missing");
        const auto finish_camera=[&] {
            for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            flush();
        };
        if(preview_only) {
            std::ifstream packet("build/form-diagnostic/CornerBodyCutPreview.json");nlohmann::json value;packet>>value;
            const auto result=document::load_body_result(value);view->set_mesh(result.mesh);flush();
        }
        for(const auto kind:{viewer::ReferenceVisibility::Origins,viewer::ReferenceVisibility::Planes,
            viewer::ReferenceVisibility::Sketches,viewer::ReferenceVisibility::Dimensions})view->set_reference_visibility(kind,false);
        const auto render=[&](const char* suffix,float rx,float ry,float rz) {
            finish_camera();
            view->fit_all();auto camera=view->camera_state();const auto rotation=QQuaternion::fromEulerAngles(rx,ry,rz);
            camera[0]=rotation.scalar();camera[1]=rotation.x();camera[2]=rotation.y();camera[3]=rotation.z();view->set_camera_state(camera);
            view->set_display_mode(viewer::DisplayMode::ShadedWithEdges);flush();
            check(view->grabFramebuffer().save(QString::fromStdString((directory/(std::string("corner-form-")+suffix+".png")).string())),"Corner rendering capture failed");
        };
        render("inside",-45,0,25);render("outside",135,0,25);
        if(preview_only){std::cout<<"Corner body-cut diagnostic rendering captured; editing and persistence not verified\n";return 0;}
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"Corner Tree missing");
        const auto row=[&]{for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&
            (*i)->data(0,Qt::UserRole+3)=="part-container")return *i;throw std::runtime_error("Corner FORM Tree row missing");};
        const auto editor=[&]() -> PrimitivePropertiesDialog* {
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<PrimitivePropertiesDialog*>(dialog);
                candidate&&candidate->isVisible()&&candidate->feature_kind()==document::FeatureKind::SheetForm)return candidate;return nullptr;
        };
        const auto open=[&]{window.show_tree_item_properties(row());flush();check(editor(),"Corner FORM Properties missing");return editor();};
        const auto closed=[&]{QElapsedTimer timer;timer.start();while(editor()&&timer.elapsed()<12000){application.processEvents();QThread::msleep(5);}flush();check(!editor(),"Corner FORM OK failed");};
        const auto saved=[&]{run("save");return document::PartDocument::load(path);};
        const auto inline_candidate=[&] {
            view->set_reference_visibility(viewer::ReferenceVisibility::Dimensions,true);
            window.show_parameter_dimensions(id);flush();
            const std::string key="parameter:placement:reference_offset:2";
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
            candidate.owner_id=id;candidate.semantic_key=key;
            bool found=false;
            for(std::size_t index=0;index<200;++index) {
                candidate.geometry_index=index;
                if(view->candidate_dimension_value(candidate)) {found=true;break;}
            }
            if(!found) {
                std::string offered;
                for(const auto& d:view->mesh().dimensions)offered+=" "+d.reference.owner_id+"/"+d.reference.semantic_key;
                throw std::runtime_error("Corner station dimension is absent from View; offered:"+offered);
            }
            return candidate;
        };
        const auto inline_number=[&](double number) {
            window.edit_dimension_inline(inline_candidate());flush();
            auto* edit=window.findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(edit&&edit->isVisible(),"Corner station has no inline View editor");
            edit->setText(QString::number(number,'g',12));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(edit,&enter);flush();
        };
        auto* dialog=open();const auto placed=dialog->pending_value();
        if(placed.placement!=original.history.back().placement)std::cerr<<nlohmann::json(placed.placement).dump()<<'\n'<<nlohmann::json(original.history.back().placement).dump()<<'\n';
        check(placed.placement==original.history.back().placement,"Corner Properties changed stored placement");
        auto* offset1=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset1");
        auto* offset2=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset2");
        auto* support_offset=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset0");
        check(support_offset&&!support_offset->isEnabled()&&support_offset->value()==0.,"Corner support offset must display disabled zero");
        check(offset1&&!offset1->isEnabled()&&offset1->value()==0.&&offset2&&offset2->isEnabled(),"Corner plane offsets are not constrained correctly");
        check(std::ranges::none_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;}),"Corner Properties failed rollback");
        dialog->set_reference_inspected(0,true);dialog->set_reference_inspected(1,true);flush();
        check(dialog->highlighted_reference_entries().size()==2,"Corner inner-plane inspection failed");
        check(window.grab().save(QString::fromStdString((directory/"corner-form-properties.png").string())),"Corner Properties capture failed");
        auto* references=dialog->findChild<QTableWidget*>("primitiveReferenceTable");
        auto* clear_support=references?references->cellWidget(0,0)->findChild<QPushButton*>():nullptr;
        check(clear_support&&clear_support->isVisible(),"Corner support has no shared clear control");
        clear_support->click();flush();
        check(!dialog->pending_value().sheet_form.support.valid()&&dialog->pending_value().placement.references.empty()&&
            !dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Cleared corner support remains confirmable");
        check(dynamic_cast<ui::ReferenceCellItem*>(references->item(0,1))->is_active_input()&&
            dialog->highlighted_reference_entries().size()==1,"Corner support clear failed entry or inspection retirement");
        check(dialog->set_reference(0,placed.placement.references[0],"Restored outer sheet face"),"Corner support cannot be restored");flush();
        check(dialog->pending_value().placement.references==placed.placement.references&&
            dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Corner support restoration lost positioning references");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(saved().history.back()==original.history.back(),"Corner Cancel changed native history");
        dialog=open();const auto before=run("documents").data;dialog->buttons()->button(QDialogButtonBox::Ok)->click();closed();
        check(run("documents").data==before,"Unchanged corner OK created an Undo revision");
        auto station_plane=placed.placement.references[2];station_plane.semantic_key="origin:plane:yz";
        station_plane.supports_offset=true;station_plane.offset=0.;
        dialog=open();check(dialog->set_reference(2,station_plane,"YZ station plane"),"Corner station plane entry failed");
        offset2=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset2");offset2->setValue(2.);flush();
        const auto draft_revision=run("documents").data;
        for(double value:{3.,-4.,5.}) {
            inline_number(value);
            check(offset2->value()==value&&dialog->pending_value().placement.references[2].offset==value&&
                std::abs(dialog->pending_value().placement.x-value)<1e-8,
                "Corner View edit did not update the authoritative FORM offset");
            check(run("documents").data==draft_revision,"Corner draft View edit committed an Undo transaction");
        }
        check(!dialog->set_inline_parameter_value("placement:reference_offset:0",1.)&&
            !dialog->set_inline_parameter_value("placement:reference_offset:1",1.)&&
            !dialog->set_inline_parameter_value("placement:reference_offset:2",1e7),
            "Corner inline entry changed a fixed plane or clamped an invalid value");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(saved().history.back()==original.history.back(),"Corner inline draft Cancel changed native history");
        dialog=open();check(dialog->set_reference(2,station_plane,"YZ station plane"),"Corner station plane re-entry failed");
        offset2=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset2");offset2->setValue(3.);flush();
        const auto pending=dialog->pending_value();check(std::abs(pending.placement.x-3.)<1e-8,"Corner longitudinal offset did not move placement");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();closed();
        check(std::abs(saved().history.back().placement.x-3.)<1e-8,"Corner OK lost longitudinal placement");
        run("undo");check(saved().history.back()==original.history.back(),"Corner placement Undo failed");
        run("redo");check(std::abs(saved().history.back().placement.x-3.)<1e-8,"Corner placement Redo failed");
        window.show_parameter_dimensions(id);flush();
        for(double value:{4.,-3.,5.}) {
            inline_number(value);const auto current=saved().history.back();
            check(current.placement.references[2].offset==value&&std::abs(current.placement.x-value)<1e-8&&
                current.sheet_form==original.history.back().sheet_form,
                "Finished corner View dimension failed placement or changed its independent definition");
        }
        const auto unchanged_revision=run("documents").data;inline_number(5.);
        check(run("documents").data==unchanged_revision,"Unchanged corner View dimension created an Undo transaction");
        for(double value:{-3.,4.,3.}) {run("undo");check(std::abs(saved().history.back().placement.x-value)<1e-8,"Corner View dimension Undo failed");}
        for(double value:{4.,-3.,5.}) {run("redo");check(std::abs(saved().history.back().placement.x-value)<1e-8,"Corner View dimension Redo failed");}
        for(int i=0;i<3;++i)run("undo");flush();
        const std::string station_key="parameter:placement:reference_offset:2";
        window.toggle_parameter_value_lock(id,station_key);flush();
        check(window.parameter_value_locked(id,station_key).value_or(false),"Corner station value lock is unavailable");
        const auto locked_revision=run("documents").data;
        window.edit_dimension_inline(inline_candidate());flush();
        check(!window.findChild<QLineEdit*>("inlineDimensionValueEdit")&&run("documents").data==locked_revision,
            "Locked corner station opened an editor or changed the document");
        window.toggle_parameter_value_lock(id,station_key);flush();
        check(!window.parameter_value_locked(id,station_key).value_or(true),"Corner station did not unlock");
        run("save");
        if(dimensions_only) {
            std::vector<kernel::BodyResult> reopened;const auto native=document::PartDocument::load(path,&reopened);
            check(std::abs(native.history.back().placement.x-3.)<1e-8&&!reopened.empty()&&
                reopened.back().calculation_errors.empty(),"Corner View edit lost its calculated native state");
            std::cout<<"Corner View dimensions: successive signed drafts and committed edits, fixed-plane rejection, Cancel, no-op, Undo/Redo and save/reopen passed\n";
            return 0;
        }
        run(QString::fromStdString(commands::Json{{"command","close"},{"arguments",{{"discard",true}}}}.dump()));
        check(window.open_document_path(QString::fromStdString(path.string())),"Corner FORM save/reopen failed");flush();
        check(std::abs(saved().history.back().placement.x-3.)<1e-8,"Corner reopen lost native placement");
        const auto choose=[&](const std::filesystem::path& file,const std::function<void()>& trigger) {
            bool selected=false;QTimer timer;timer.setInterval(30);
            QObject::connect(&timer,&QTimer::timeout,&window,[&] {
                for(auto* widget:QApplication::topLevelWidgets())if(auto* picker=qobject_cast<QFileDialog*>(widget);picker&&picker->isVisible()) {
                    check(picker->nameFilters().size()==1&&picker->nameFilters().front().contains("*.prtz"),"Corner picker offered non-Part files");
                    picker->selectFile(QString::fromStdString(file.string()));
                    if(auto* field=picker->findChild<QLineEdit*>("fileNameEdit"))field->setText(QString::fromStdString(file.string()));
                    selected=true;timer.stop();QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);return;
                }
            });timer.start();trigger();timer.stop();check(selected,"Corner file picker missing");flush();
        };
        const auto definition=document::stored_sheet_form_definition(original.history.back().sheet_form);
        auto replacement=definition.part;replacement.name="Replacement corner gusset";
        const auto replacement_path=directory/"corner-form-replacement.prtz";replacement.save(replacement_path,*definition.calculated);
        const auto existing=saved().history.back().sheet_form;dialog=open();
        choose(replacement_path,[&]{dialog->findChild<QPushButton*>("sheetFormReplaceDefinition")->click();});
        check(dialog->pending_value().sheet_form.source_name==replacement.name,"Corner replacement was not loaded");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(saved().history.back().sheet_form==existing,"Corner replacement Cancel changed definition");
        dialog=open();choose(replacement_path,[&]{dialog->findChild<QPushButton*>("sheetFormReplaceDefinition")->click();});
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();closed();
        check(saved().history.back().sheet_form.source_name==replacement.name,"Corner replacement OK lost definition");
        run("undo");check(saved().history.back().sheet_form==existing,"Corner replacement Undo failed");
        run("redo");check(saved().history.back().sheet_form.source_name==replacement.name,"Corner replacement Redo failed");
        auto stock=original;stock.erase_history_object(id);kernel::OcctKernel kernel;
        const auto stock_result=workspace::calculate_part_with_resolved_references(kernel,stock);
        const auto stock_path=directory/"corner-form-create-ui.prtz";stock.save(stock_path,stock_result);
        run(QString::fromStdString(commands::Json{{"command","close"},{"arguments",{{"discard",true}}}}.dump()));
        check(window.open_document_path(QString::fromStdString(stock_path.string())),"Corner creation stock did not open");flush();
        run(QString::fromStdString("body.activate "+stock.body_history.bodies().front().scope.id));mode->trigger();flush();
        const auto source=std::filesystem::absolute("config/lib/01-SHEETMETAL/01-FORM/CornerGusset90.prtz");
        auto* action=window.findChild<QAction*>("sheetFormAction");check(action&&action->isEnabled(),"Corner Form action unavailable");
        choose(source,[&]{action->trigger();});dialog=editor();check(dialog,"Corner creation Properties missing");
        const auto canceled=dialog->container_id();dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        run("save");check(!document::PartDocument::load(stock_path).find_container(canceled),"Corner creation Cancel inserted history");
        choose(source,[&]{action->trigger();});dialog=editor();check(dialog,"Corner creation Properties missing");
        const auto created_id=dialog->container_id();const auto mouse=[&](QPointF point,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held) {
            QMouseEvent event(type,point,QPointF(view->mapToGlobal(point.toPoint())),button,held,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        for(unsigned reference=0;reference<2;++reference) {
            const auto& wanted=original.history.back().placement.references[reference];bool found=false;
            for(float rx:{135.f,-45.f,45.f,-135.f}) {
                finish_camera();
                view->fit_all();auto camera=view->camera_state();const auto rotation=QQuaternion::fromEulerAngles(rx,0,25);
                camera[0]=rotation.scalar();camera[1]=rotation.x();camera[2]=rotation.y();camera[3]=rotation.z();view->set_camera_state(camera);flush();
                for(int y=15;y<view->height()&&!found;y+=25)for(int x=15;x<view->width()&&!found;x+=25) {
                    const QPointF point{double(x),double(y)};
                    const auto matches=[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==wanted.owner_id&&candidate.semantic_key==wanted.semantic_key;};
                    const auto offered=view->selection_candidates_at(point);
                    if(std::ranges::none_of(offered,matches))continue;
                    mouse(point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                    auto candidate=view->hovered_candidate();
                    for(std::size_t cycle=0;candidate&&!matches(*candidate)&&cycle<offered.size();++cycle) {
                        mouse(point,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);
                        mouse(point,QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
                        candidate=view->hovered_candidate();
                    }
                    check(candidate&&matches(*candidate),"Corner RMB did not offer the expected outer plane");
                    mouse(point,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(point,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                    const auto pending=dialog->pending_value();
                    check(pending.placement.references.at(reference).owner_id==candidate->owner_id&&pending.placement.references.at(reference).semantic_key==candidate->semantic_key,
                        "Corner click differs from hovered plane");found=true;
                }
                if(found)break;
            }
            check(found,"Corner picker did not offer the expected outer plane");
        }
        const auto point=original.history.back().placement.references[2];
        check(dialog->set_reference(2,point,"Bend station datum"),"Corner station entry failed");flush();
        check(dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Complete corner creation cannot confirm");
        check(std::ranges::none_of(view->mesh().axes,[&](const auto& axis){return axis.reference.owner_id==created_id&&axis.reference.semantic_key=="axis";}),
            "Corner preview retained arbitrary placement axis");
        QElapsedTimer confirmation;confirmation.start();dialog->buttons()->button(QDialogButtonBox::Ok)->click();closed();
        std::cout<<"Corner GUI confirmation milliseconds="<<confirmation.elapsed()<<'\n';run("save");
        std::vector<kernel::BodyResult> created_cache;auto created=document::PartDocument::load(stock_path,&created_cache);
        check(created.find_container(created_id),"Corner GUI creation did not persist");
        check(!created_cache.empty()&&created_cache.back().calculation_errors.empty()&&
            std::abs(created_cache.back().volume-stock_result.back().volume)>1.,"Corner GUI creation retained an error or unchanged stock");
        check(std::ranges::count_if(created_cache.back().mesh.axes,[&](const auto& axis) {
            return axis.reference.owner_id==created_id&&axis.reference.semantic_key.starts_with("centerline:from:centroid:form:");
        })==1,"Corner GUI creation did not publish one whole-profile centroid axis");
        view->set_reference_visibility(viewer::ReferenceVisibility::Axes,true);flush();
        render("created-inside",-45,0,25);render("created-outside",135,0,25);
        run("undo");run("save");check(!document::PartDocument::load(stock_path).find_container(created_id),"Corner creation Undo failed");
        run("redo");run("save");check(document::PartDocument::load(stock_path).find_container(created_id),"Corner creation Redo failed");
        run(QString::fromStdString(commands::Json{{"command","close"},{"arguments",{{"discard",true}}}}.dump()));
        check(window.open_document_path(QString::fromStdString(stock_path.string())),"Created corner could not reopen");flush();
        const auto copy_row=[&](const std::string& wanted){for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole).toString().toStdString()==wanted&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
            return static_cast<QTreeWidgetItem*>(nullptr);};
        for(bool pattern:{true,false}) {
            check(copy_row(created_id),"Corner copy source missing from Tree");tree->setCurrentItem(copy_row(created_id));flush();
            auto* action=window.findChild<QAction*>(pattern?"patternAction":"mirrorAction");
            check(action&&action->isEnabled(),"Corner copy command disabled");action->trigger();flush();
            auto* copy=dynamic_cast<DerivedCopyDialog*>(window.findChild<QDialog*>(pattern?"patternDialog":"mirrorDialog"));
            check(copy&&copy->derived_copy.source_id==created_id,"Corner copy did not prefill selected feature");
            if(pattern) {
                check(copy->derived_copy.pattern->linear[0].local_axis==0,"Default Pattern direction is not X");
                copy->findChild<QSpinBox*>("patternCount0")->setValue(2);
                copy->findChild<QDoubleSpinBox*>("patternSpacing0")->setValue(30.);
            }else {
                copy->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(15.);
                copy->findChild<QPushButton*>("mirrorPlane_yz")->click();
            }
            flush();const auto copy_id=copy->pending.id;
            check(!view->transient_edges().empty(),"Corner copy preview is empty");
            for(auto edge:view->transient_edges()) {
                constexpr std::string_view split="boolean:form:split-edge:from:";
                check(!edge.reference.semantic_key.starts_with(split)||
                    edge.reference.semantic_key.starts_with(std::string(split)+std::to_string(created_id.size())+":"+created_id),
                    "Corner copy preview translated a supporting stock-edge fragment");
                edge.overlay=false;
                check(!edge.parameter_seam&&!kernel::smooth_transition_junction(edge),"Corner copy preview exposed a hidden surface seam or smooth joint");
            }
            render(pattern?"pattern-preview":"mirror-preview",135,0,25);
            check(copy->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Corner copy cannot confirm");
            QElapsedTimer copied;copied.start();copy->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!window.findChild<QDialog*>(pattern?"patternDialog":"mirrorDialog"),"Corner copy failed to close after OK");
            std::cout<<(pattern?"Corner GUI Pattern":"Corner GUI Mirror")<<" milliseconds="<<copied.elapsed()<<'\n';
            check(copy_row(copy_id),"Corner copy did not commit to Tree");run("save");
            std::vector<kernel::BodyResult> copied_cache;const auto copied_part=document::PartDocument::load(stock_path,&copied_cache);
            check(copied_part.find_container(copy_id)&&copied_cache.back().calculation_errors.empty(),"Corner copy persisted an error");
            window.show_tree_item_properties(copy_row(copy_id));flush();
            copy=dynamic_cast<DerivedCopyDialog*>(window.findChild<QDialog*>(pattern?"patternDialog":"mirrorDialog"));
            check(copy,"Corner copy Properties cannot reopen");
            const auto unchanged=run("documents").data;copy->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(run("documents").data==unchanged,"Unchanged corner copy OK created an Undo revision");
            run("undo");check(!copy_row(copy_id),"Corner copy Undo failed");run("redo");check(copy_row(copy_id),"Corner copy Redo failed");
            run("undo");
        }
        for(bool unfold:{true,false}) {
            auto* action=window.findChild<QAction*>(unfold?"unbendAction":"bendBackAction");
            check(action&&action->isEnabled(),"Corner material-state command unavailable");action->trigger();flush();
            auto* state=window.findChild<QDialog*>("sheetStateDialog");check(state,"Corner material-state Properties missing");
            state->findChild<QCheckBox*>("sheetStateAll")->setChecked(true);flush();
            state->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(!window.findChild<QDialog*>("sheetStateDialog"),"Corner material state did not commit");
            const auto axes=std::ranges::count_if(view->mesh().axes,[&](const auto& axis) {
                return axis.reference.owner_id==created_id&&kernel::sheet_material::is_form_centroid_axis(axis.reference);
            });
            std::cout<<"Corner GUI "<<(unfold?"Unbend":"Bend Back")<<" centroid axes="<<axes<<'\n';
            if(axes!=(unfold?0:1)) {run("save");std::cout<<"Corner material-state diagnostic="<<stock_path.string()<<'\n';}
            check(axes==(unfold?0:1),"Corner material state displayed the wrong centroid axis");
            if(unfold) {
                check(std::ranges::none_of(view->mesh().edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("form:symbol:");}),
                    "Corner formed during bending displayed a flat symbol");
                render("created-flat",-45,0,25);
            }
        }
        std::cout<<"Corner FORM GUI: creation, centroid axis, exact hover/click planes, definition replacement, rollback/inspection, Cancel, unchanged OK, longitudinal placement, Pattern/Mirror, Unbend/Bend Back display, Undo/Redo and save/reopen passed\n";return 0;
    }catch(const std::exception& error){window.grab().save(QString::fromStdString((directory/"corner-form-failure.png").string()));std::cerr<<"Corner FORM GUI: "<<error.what()<<'\n';return 1;}
}

Q_NEVER_INLINE static int verify_sheet_form_ui(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const QString& command){auto result=window.execute_console_command(command);if(!result.ok)throw std::runtime_error(result.message);return result;};
        auto part=document::PartDocument::create_default();
        static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Sheet"));
        auto stock=document::PartDocument::create_sketch_container();stock.feature_kind=document::FeatureKind::Flat;
        stock.flat.direction=document::ExtrusionDirection::Reverse;stock.flat.thickness_override=true;stock.flat.thickness=1;
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=stock.id;
        static_cast<void>(outline.add_rectangle(-150,-150,150,150));stock.flat.sketch_id=outline.id;
        part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);part.history.push_back(stock);part.sketches.push_back(outline);
        kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto path=directory/("sheet-form-ui-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()+".prtz");
        part.save(path,calculated);
        const auto source=std::filesystem::absolute("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
        const auto replacement=directory/"sheet-form-replacement.prtz";
        const auto definition=document::read_sheet_form_definition(source);auto renamed=definition.part;
        renamed.name="Replacement Ventilation Window";renamed.save(replacement,*definition.calculated);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"FORM stock could not open");flush();
        check(part.body_owner_for_object(stock.id)!=nullptr,"FORM stock has no owning Body");
        run(QString::fromStdString("body.activate "+part.body_owner_for_object(stock.id)->scope.id));flush();
        auto* mode=window.findChild<QAction*>("applicationModeAction2");check(mode&&mode->isEnabled(),"Sheet workspace unavailable");mode->trigger();flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM View missing");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"FORM Tree missing");
        const auto editor=[&]() -> PrimitivePropertiesDialog* {
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<PrimitivePropertiesDialog*>(dialog);
                candidate&&candidate->isVisible()&&candidate->feature_kind()==document::FeatureKind::SheetForm)return candidate;
            return nullptr;
        };
        const auto wait_closed=[&]{QElapsedTimer timer;timer.start();while(editor()&&timer.elapsed()<12000){application.processEvents();QThread::msleep(5);}flush();check(!editor(),"FORM OK failed to close its dialog");};
        const auto choose=[&](const std::filesystem::path& file,const std::function<void()>& trigger){
            bool selected=false;QTimer timer;timer.setInterval(30);
            QObject::connect(&timer,&QTimer::timeout,&window,[&]{
                for(auto* widget:QApplication::topLevelWidgets())if(auto* picker=qobject_cast<QFileDialog*>(widget);picker&&picker->isVisible()) {
                    check(picker->nameFilters().size()==1&&picker->nameFilters().front().contains("*.prtz"),"FORM picker did not filter native Parts");
                    picker->selectFile(QString::fromStdString(file.string()));
                    if(auto* field=picker->findChild<QLineEdit*>("fileNameEdit"))field->setText(QString::fromStdString(file.string()));
                    selected=true;timer.stop();QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);return;
                }
            });timer.start();trigger();timer.stop();check(selected,"FORM file picker did not open");flush();
        };
        const auto open=[&]{auto* action=window.findChild<QAction*>("sheetFormAction");check(action&&action->isEnabled(),"FORM action unavailable");
            choose(source,[&]{action->trigger();});auto* dialog=editor();check(dialog,"FORM Properties missing");return dialog;};
        const auto native=[&]{run("save");std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);return std::pair{std::move(saved),std::move(cache)};};
        const auto row=[&](const std::string& id){QTreeWidgetItem* found{};for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){found=*i;break;}check(found,"FORM Tree row missing");return found;};
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held){
            QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        auto* dialog=open();const auto canceled=dialog->container_id();
        check(!dialog->findChild<QPushButton*>("containerDefaultOriginButton"),"Form exposes a generic Default action");
        if(const auto* origin=dialog->findChild<QPushButton*>("containerOriginSelectionButton"))
            check(!origin->isVisible(),"Form exposes a generic Origin action");
        const auto* adapter=dialog->findChild<QWidget*>("sheetFormNativePlacementAdapter");
        check(adapter&&!adapter->isVisible(),"Form exposes its generic placement adapter");
        check(dialog->findChild<QDoubleSpinBox*>("sheetFormOffset1")&&dialog->findChild<QDoubleSpinBox*>("sheetFormOffset2"),"Form positioning offsets are missing");
        auto* support_offset=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset0");
        check(support_offset&&!support_offset->isEnabled()&&support_offset->value()==0.,"FORM support offset must display disabled zero");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(!native().first.find_container(canceled),"FORM creation Cancel inserted history");
        dialog=open();check(dialog->windowFlags().testFlag(Qt::SubWindow),"FORM Properties are not an internal SubWindow");
        std::optional<viewer::ViewerCandidate> hovered;QPointF click;
        for(int y=view->height()/3;y<view->height()*2/3&&!hovered;y+=35)for(int x=view->width()/2;x<view->width()*4/5&&!hovered;x+=35) {
            click={double(x),double(y)};mouse(click,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
            if(const auto candidate=view->hovered_candidate();candidate&&candidate->kind==viewer::CandidateKind::Face)hovered=candidate;
        }
        check(hovered.has_value(),"FORM common picker did not offer a sheet face");
        mouse(click,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(click,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        auto pending=dialog->pending_value();check(pending.sheet_form.support.owner_id==hovered->owner_id&&
            pending.sheet_form.support.semantic_key==hovered->semantic_key,"FORM click did not preserve the hovered face identity");
        check(pending.placement.references.size()==4&&pending.placement.references[1].owner_id.empty()&&
            pending.placement.references[2].owner_id.empty(),"FORM face selection prefilled positioning references");
        check(!dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Incomplete Form support enabled confirmation");
        dialog->set_reference_inspected(0,true);flush();
        check(dialog->highlighted_reference_entries().size()==1,"FORM face inspection did not remain independent");
        dialog->set_reference_inspected(0,false);flush();
        const auto first_position=kernel::Vec3{pending.placement.x,pending.placement.y,pending.placement.z};
        auto* placement_table=dialog->findChild<QTableWidget*>("primitiveReferenceTable");
        check(placement_table,"FORM shared reference table missing");
        check(placement_table->columnCount()==4,"FORM retained its redundant geometry-description column");
        check(!placement_table->cellWidget(0,0)->findChild<QLabel*>()->isVisible(),
            "Completed required FORM support retained its entry arrow");
        check(placement_table->cellWidget(0,0)->findChild<QPushButton*>()->isVisible(),
            "Completed FORM support has no shared clear control");
        const QPointF input_point(placement_table->visualItemRect(placement_table->item(0,1)).center());
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,input_point,QPointF(placement_table->viewport()->mapToGlobal(input_point.toPoint())),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(placement_table->viewport(),&event);flush();
        }
        bool replaced_face=false;
        for(int shift:{35,-35,70,-70}) {
            const QPointF replacement_point{click.x()+shift,click.y()};
            mouse(replacement_point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
            const auto offered=view->hovered_candidate();
            if(!offered||offered->kind!=viewer::CandidateKind::Face)continue;
            mouse(replacement_point,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
            mouse(replacement_point,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
            pending=dialog->pending_value();
            check(pending.sheet_form.support.owner_id==offered->owner_id&&pending.sheet_form.support.semantic_key==offered->semantic_key,
                "FORM replacement changed the hovered face identity");
            const auto delta=kernel::sheet_material::sub({pending.placement.x,pending.placement.y,pending.placement.z},first_position);
            replaced_face=kernel::sheet_material::dot(delta,delta)>1e-8;break;
        }
        check(replaced_face,"FORM face replacement retained the old click position");
        check(pending.placement.references[1].owner_id.empty()&&pending.placement.references[2].owner_id.empty(),
            "FORM face replacement generated unwanted datum planes");
        const auto point_owner=part.body_owner_for_object(stock.id)->origin().id;
        const document::ConstructionReference point_reference{{},point_owner,"origin:point"};
        check(dialog->set_reference(1,point_reference,"Sheet datum point")&&
            dialog->set_reference(2,point_reference,"Sheet datum point"),"FORM manual coordinates failed");flush();
        pending=dialog->pending_value();
        const auto position=kernel::Vec3{pending.placement.x,pending.placement.y,pending.placement.z};
        const auto outward=kernel::sheet_material::mul(pending.sheet_form.support.surface->axis,pending.sheet_form.support.surface->reversed?-1.:1.);
        const auto revision=run("documents").data;const auto camera=view->camera_state();
        auto* angle=dialog->findChild<QDoubleSpinBox*>("sheetFormRotation");check(angle&&angle->isEnabled(),"FORM in-plane rotation unavailable");
        const auto zero_tangent=document::construction_direction_from_local_axis("x",
            {pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z});
        for(double value:{30.,-40.,-370.,-10.,15.}) {angle->setValue(value);flush();pending=dialog->pending_value();
            check(std::abs(pending.placement.x-position.x)<1e-8&&std::abs(pending.placement.y-position.y)<1e-8&&std::abs(pending.placement.z-position.z)<1e-8,"FORM rotation moved its insertion point");
            const auto normal=document::construction_direction_from_local_axis("y",{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z});
            check(kernel::sheet_material::dot(normal,outward)>1.-1e-8,"FORM rotation tilted away from the support");
            const auto radians=value*std::numbers::pi/180.;
            const auto expected=kernel::sheet_material::add(kernel::sheet_material::mul(zero_tangent,std::cos(radians)),
                kernel::sheet_material::mul(kernel::sheet_material::cross(outward,zero_tangent),std::sin(radians)));
            const auto tangent=document::construction_direction_from_local_axis("x",
                {pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z});
            check(std::abs(pending.placement.absolute_rotation_y-value)<1e-8&&
                kernel::sheet_material::dot(tangent,expected)>1.-1e-8,"FORM angle did not rotate its native frame");
            check(!view->transient_edges().empty(),"FORM rotation lost its native wire preview");
        }
        check(run("documents").data==revision&&view->camera_state()==camera,"FORM preview changed a document revision or camera");
        dialog->set_reference_inspected(1,true);flush();
        for(int index:{1,2}) {
            auto* clear=placement_table->cellWidget(index,0)->findChild<QPushButton*>();
            check(clear&&clear->isVisible(),"Form positioning reference cannot be cleared");
            clear->click();flush();
        }
        check(!dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled()&&
            dialog->highlighted_reference_entries().empty(),"Incomplete Form references retained confirmation or inspection");
        check(placement_table->rowCount()==3,"Clearing a Form reference deleted its structural row");
        check(dialog->set_reference(1,point_reference,"Sheet datum point")&&
            dialog->set_reference(2,point_reference,"Sheet datum point"),"Form rejected independent coordinates from one native point");flush();
        check(dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Restoring Form references did not restore confirmation");
        auto* offset1=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset1");
        auto* offset2=dialog->findChild<QDoubleSpinBox*>("sheetFormOffset2");check(offset1&&offset2,"Form offsets missing");
        // The native Body-local resolver owns the sheet frame; inspect its
        // actual zero-angle preview rather than an untransformed cache packet.
        angle->setValue(0);flush();const auto zero_point=dialog->pending_value().placement;
        const auto coordinate_x=document::construction_direction_from_local_axis("x",{zero_point.rotation_x,zero_point.rotation_y,zero_point.rotation_z});
        const auto coordinate_z=document::construction_direction_from_local_axis("z",{zero_point.rotation_x,zero_point.rotation_y,zero_point.rotation_z});
        angle->setValue(-370);flush();
        for(const auto values:{std::array{12.,-8.},std::array{-15.,7.},std::array{0.,0.},std::array{12.,-8.}}) {
            offset1->setValue(values[0]);offset2->setValue(values[1]);flush();pending=dialog->pending_value();
            const kernel::Vec3 origin{pending.placement.x,pending.placement.y,pending.placement.z};
            check(std::abs(kernel::sheet_material::dot(origin,coordinate_x)-values[0])<1e-8&&
                std::abs(kernel::sheet_material::dot(origin,coordinate_z)-values[1])<1e-8,
                "Form point offsets do not independently control its planar coordinates");
            check(kernel::sheet_material::dot(document::construction_direction_from_local_axis("y",{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z}),outward)>1.-1e-8,
                "Form offset edit tilted its sheet normal");
        }
        check(run("documents").data==revision,"Form reference/offset drafts committed a revision");
        const auto complete_references=dialog->pending_value().placement.references;
        dialog->set_reference_inspected(0,true);flush();
        placement_table->cellWidget(0,0)->findChild<QPushButton*>()->click();flush();
        check(!dialog->pending_value().sheet_form.support.valid()&&dialog->pending_value().placement.references.empty()&&
            !dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled()&&!angle->isEnabled(),"Cleared FORM support retained confirmation or rotation");
        check(dialog->highlighted_reference_entries().empty()&&
            dynamic_cast<ui::ReferenceCellItem*>(placement_table->item(0,1))->is_active_input(),"FORM clear failed inspection retirement or entry");
        check(dialog->set_reference(0,complete_references[0],"Restored sheet face"),"FORM support cannot be restored");flush();
        check(dialog->pending_value().placement.references==complete_references&&
            dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled()&&angle->isEnabled(),"FORM support restoration lost references or offsets");
        check(run("documents").data==revision,"FORM support clearing created a document revision");
        pending=dialog->pending_value();
        const auto before_resize=placement_table->geometry();
        dialog->resize(dialog->width()+80,dialog->height()+80);flush();
        check(placement_table->geometry().y()==before_resize.y()&&placement_table->height()==before_resize.height(),
            "Form resizing redistributed space between reference rows");
        window.grab().save(QString::fromStdString((directory/"sheet-form-placement.png").string()));
        const std::array preview_axes{
            document::construction_direction_from_local_axis("x",{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z}),
            document::construction_direction_from_local_axis("y",{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z}),
            document::construction_direction_from_local_axis("z",{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z})};
        const auto preview_wire=view->transient_edges();const auto local_wire=dialog->sheet_form_preview();
        const auto id=pending.id;QElapsedTimer elapsed;elapsed.start();dialog->buttons()->button(QDialogButtonBox::Ok)->click();wait_closed();
        std::cout<<"FORM GUI confirmed insertion="<<elapsed.elapsed()/1000.<<" s"<<std::endl;
        auto [saved,cache]=native();check(saved.find_container(id)&&cache.back().calculation_errors.empty(),"FORM GUI creation retained a calculation error");
        check(saved.find_container(id)->placement.references==pending.placement.references,"FORM OK lost its attachment references");
        const auto& confirmed=saved.find_container(id)->placement;
        const auto axis_mesh=saved.construction_viewer_mesh();
        const auto axis=std::ranges::find_if(axis_mesh.axes,[&](const auto& a){return a.reference.owner_id==id&&a.reference.semantic_key=="axis";});
        check(axis!=axis_mesh.axes.end()&&kernel::sheet_material::dot(axis->direction,outward)>1.-1e-8,
            "FORM result axis is not normal to the support");
        check(std::abs(axis->display_length-10.)<1e-9,"FORM result axis is not the requested 10 mm");
        check(std::ranges::none_of(axis_mesh.points,[&](const auto& p){return p.reference.owner_id==id&&p.reference.semantic_key.starts_with("axis:point:");}),
            "FORM result axis exposes endpoint handles");
        check(std::abs(confirmed.x-pending.placement.x)<1e-8&&
            std::abs(confirmed.y-pending.placement.y)<1e-8&&std::abs(confirmed.z-pending.placement.z)<1e-8,
            "FORM confirmation moved its previewed insertion point");
        const std::array confirmed_axes{
            document::construction_direction_from_local_axis("x",{confirmed.rotation_x,confirmed.rotation_y,confirmed.rotation_z}),
            document::construction_direction_from_local_axis("y",{confirmed.rotation_x,confirmed.rotation_y,confirmed.rotation_z}),
            document::construction_direction_from_local_axis("z",{confirmed.rotation_x,confirmed.rotation_y,confirmed.rotation_z})};
        for(std::size_t axis=0;axis<preview_axes.size();++axis)
            check(kernel::sheet_material::dot(preview_axes[axis],confirmed_axes[axis])>1.-1e-8,
                "FORM confirmation changed its previewed native frame");
        check(preview_wire.size()==local_wire.size(),"FORM preview wire lost definition edges");
        for(std::size_t edge=0;edge<local_wire.size();++edge) {
            check(preview_wire[edge].points.size()==local_wire[edge].points.size(),"FORM preview changed curve samples");
            for(std::size_t index=0;index<local_wire[edge].points.size();++index) {
                const auto point=local_wire[edge].points[index];auto expected=kernel::Vec3{confirmed.x,confirmed.y,confirmed.z};
                for(std::size_t axis=0;axis<3;++axis)expected=kernel::sheet_material::add(expected,
                    kernel::sheet_material::mul(confirmed_axes[axis],axis==0?point.x:axis==1?point.y:point.z));
                const auto delta=kernel::sheet_material::sub(preview_wire[edge].points[index],expected);
                check(kernel::sheet_material::dot(delta,delta)<1e-12,"FORM preview curve disagrees with its confirmed native frame");
            }
        }
        const auto original=saved.find_container(id)->sheet_form;const auto placed=saved.find_container(id)->placement;
        run("undo");check(!native().first.find_container(id),"FORM creation Undo failed");run("redo");check(native().first.find_container(id),"FORM creation Redo failed");
        window.show_tree_item_properties(row(id));flush();dialog=editor();check(dialog,"FORM edit Properties missing");
        const auto* reopened_table=dialog->findChild<QTableWidget*>("primitiveReferenceTable");
        check(reopened_table&&reopened_table->item(1,1)->text()==QObject::tr("Bod")&&
            reopened_table->item(2,1)->text()==QObject::tr("Bod")&&reopened_table->item(0,1)->text().size()<40,
            "Reopened Form exposes topology identifiers instead of readable reference labels");
        check(std::ranges::none_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;})&&
            std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==stock.id;}),"FORM rollback failed");
        const auto unchanged_revision=run("documents").data;
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();wait_closed();check(run("documents").data==unchanged_revision,"Unchanged FORM OK created an Undo revision");
        window.show_tree_item_properties(row(id));flush();dialog=editor();check(dialog,"FORM replacement Properties missing");
        choose(replacement,[&]{dialog->findChild<QPushButton*>("sheetFormReplaceDefinition")->click();});
        check(dialog->pending_value().sheet_form.source_name==renamed.name,"FORM pending replacement did not load the new definition");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(native().first.find_container(id)->sheet_form==original,"FORM replacement Cancel changed its definition");
        window.show_tree_item_properties(row(id));flush();dialog=editor();
        choose(replacement,[&]{dialog->findChild<QPushButton*>("sheetFormReplaceDefinition")->click();});
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();wait_closed();
        saved=native().first;check(saved.find_container(id)->sheet_form.source_name==renamed.name&&saved.find_container(id)->placement==placed,"FORM replacement changed feature placement");
        run("undo");check(native().first.find_container(id)->sheet_form==original,"FORM replacement Undo failed");run("redo");
        check(native().first.find_container(id)->sheet_form.source_name==renamed.name,"FORM replacement Redo failed");
        window.grab().save(QString::fromStdString((directory/"sheet-form-ui.png").string()));
        run(QString::fromStdString(commands::Json{{"command","close"},{"arguments",{{"discard",true}}}}.dump()));
        check(window.open_document_path(QString::fromStdString(path.string())),"Inserted FORM could not reopen");flush();
        check(native().first.find_container(id)->sheet_form.source_name==renamed.name,"FORM save/reopen lost its independent definition");
        // The authored FORM shell is also a representative connected, multi-
        // patch input for ordinary Thicken Surface, using the real GUI picker.
        const auto skin_path=directory/"sheet-form-thicken-copy.prtz";
        definition.part.save(skin_path,*definition.calculated);
        check(window.open_document_path(QString::fromStdString(skin_path.string())),"Connected FORM shell cannot open");flush();
        run(QString::fromStdString("body.activate "+definition.bodies[1]));flush();view->fit_all();flush();
        auto* thicken_action=window.findChild<QAction*>("surfaceThickenAction");
        check(thicken_action&&thicken_action->isEnabled(),"Connected shell thickening unavailable");thicken_action->trigger();flush();
        const auto thick_editor=[&]{return dynamic_cast<SurfaceThickenDialog*>(window.findChild<QWidget*>("surfaceThickenDialog"));};
        check(thick_editor(),"Connected shell properties missing");
        bool selected_shell=false;
        for(int y=view->height()/4;y<view->height()*3/4&&!selected_shell;y+=20)
            for(int x=view->width()/3;x<view->width()*4/5&&!selected_shell;x+=20) {
                const QPointF p{double(x),double(y)};mouse(p,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                if(const auto offered=view->hovered_candidate();offered&&offered->kind==viewer::CandidateKind::Face) {
                    mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                    mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                    selected_shell=thick_editor()->pending.surface_thicken.face.valid();
                }
            }
        check(selected_shell&&thick_editor()->inspected(),"Connected shell picking or inspection failed");
        const auto thick_id=thick_editor()->pending.id;
        window.grab().save(QString::fromStdString((directory/"sheet-form-whole-shell-inspection.png").string()));
        thick_editor()->buttons()->button(QDialogButtonBox::Ok)->click();
        QElapsedTimer thick_wait;thick_wait.start();
        while(thick_editor()&&thick_wait.elapsed()<30000){application.processEvents();QThread::msleep(5);}flush();
        check(!thick_editor(),"Connected shell thickening did not confirm");run("save");
        std::vector<kernel::BodyResult> thick_cache;const auto thickened=document::PartDocument::load(skin_path,&thick_cache);
        const auto& thick_body=thick_cache.back().body_outputs.at(definition.bodies[1]).get();
        check(thickened.find_container(thick_id)&&thick_body.volume>0&&thick_body.calculation_errors.empty()&&
            std::ranges::none_of(thick_body.mesh.triangle_references,[](const auto& face){return face.surface_result;}),
            "GUI thickening left unconsumed shell patches");
        std::cout<<"FORM GUI create, native face pick, planar rotation, rollback, unchanged OK, replacement, Cancel, Undo/Redo and save/reopen passed"<<std::endl;return 0;
    }catch(const std::exception& failure){window.grab().save(QString::fromStdString((directory/"sheet-form-ui-failure.png").string()));std::cerr<<"FORM GUI: "<<failure.what()<<std::endl;return 1;}
}
