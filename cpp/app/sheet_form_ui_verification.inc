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
