                {
                    const auto choose_combined=[&](FemBodyDialog* form) {
                        auto* type=form->findChild<QComboBox*>("femAnalysisKind");type->setCurrentIndex(type->findData(int(document::FemAnalysisKind::ThermalMechanical)));flush();
                        check(!form->findChild<QCheckBox*>("femThermalExpansion")->isVisible()&&form->findChild<QDoubleSpinBox*>("femAmbientTemperature")->isVisible(),"Combined settings did not expose the right controls");
                    };
                    analysis_settings=body_settings();choose_combined(analysis_settings);analysis_settings->reject();flush();
                    check(window.fem_analysis_kind()==document::FemAnalysisKind::SteadyStateThermal,"Combined Cancel changed mode");
                    analysis_settings=body_settings();choose_combined(analysis_settings);analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                    check(window.fem_expansion_analysis()&&!window.active_generated_fem_mesh(),"Combined mode did not invalidate load regions");
                    for(auto* name:{"femForceAction","femPressureAction","femHeatSourceAction","femConstraintAction","femMeasurementDistanceAction"})
                        check(current_fem_action(name)->isEnabled(),"Combined command missing");
                    const auto saved_pressure=*window.active_fem_condition(tip.id);
                    auto pressure=tip;pressure.kind=document::FemConditionKind::Pressure;pressure.name="Combined pressure";
                    pressure.pressure_MPa=2.5;pressure.placement.rotation_y=0;
                    if(mode) {
                        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=pressure.id;
                        sketch.plane_auto=false;sketch.plane_offset=span/2+(mode==2?5:0);
                        static_cast<void>(sketch.add_rectangle(-3,-2,3,2));sketch.refresh_default_frame();
                        pressure.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{},{},mode==2};
                        if(mode==1)pressure.references.clear();else pressure.projected_sketch->surface_seeds={{0,0,span/2}};
                    }
                    static_cast<void>(workspace::commit_fem_condition(window.workspace_,workspace::prepare_fem_edit(pressure_session.document(),beam_fem),pressure));
                    // Independent force and pressure share the same mechanical solve.
                    auto force=tip;force.id="combined-force";force.name="Combined force";
                    static_cast<void>(workspace::commit_fem_condition(window.workspace_,workspace::prepare_fem_edit(pressure_session.document(),beam_fem),force));
                    window.show_fem_condition_properties(document::FemConditionKind::Force,force.id);flush();
                    auto* force_form=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                    check(force_form&&force_form->set_inline_parameter_value("placement:rotation_y",90),"Combined transverse force direction is not editable");
                    force_form->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                    window.refresh_scene();generate->trigger();flush();
                    if(!window.active_generated_fem_mesh())throw std::runtime_error("Combined GUI mesh failed: "+window.state_->text().toStdString());
                    solve->trigger();flush();
                    if(!window.active_fem_deformation())throw std::runtime_error("Combined GUI solve failed: "+window.state_->text().toStdString());
                    const auto* combined_mesh=window.active_generated_fem_mesh();const auto* combined_result=window.active_fem_deformation();
                    const auto combined_vertices=combined_result->vertices;
                    const auto combined_revision=pressure_session.revision();analysis_settings=body_settings();
                    analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                    check(pressure_session.revision()==combined_revision&&window.active_fem_deformation()==combined_result,"Unchanged combined OK recalculated or created Undo");
                    const auto combined_definition=*pressure_session.document().body_history.find(beam_fem)->fem;
                    const auto exact_mesh=generate_fem_volume_mesh(solid.get(),combined_definition);
                    const auto temperatures=solve_fem_steady_thermal(exact_mesh,combined_definition,fem_thermal_material(thermal_material));
                    const FemThermalStrain strain{fem_expansion_material(thermal_material),temperatures.temperature_C};
                    auto thermal_only=combined_definition;thermal_only.analysis=document::FemAnalysisKind::SteadyStateThermal;thermal_only.thermal->expansion=true;
                    const auto heat_result=solve_fem_linear_statics(exact_mesh,thermal_only,{young,poisson},{},&strain);
                    auto mechanical_only=combined_definition;mechanical_only.analysis=document::FemAnalysisKind::LinearElasticStatic;
                    const auto mechanical_result=solve_fem_linear_statics(exact_mesh,mechanical_only,{young,poisson});
                    check(std::ranges::any_of(mechanical_result.displacement,[](const auto& u){return std::abs(u.x)>.01;}),"Combined fixture lost its transverse bending force");
                    check(exact_mesh.surface.vertices==combined_mesh->vertices,"Combined GUI comparison used another mesh");
                    const auto repeated_combined=solve_fem_linear_statics(exact_mesh,combined_definition,{young,poisson},{},&strain);
                    check(fem_deformed_mesh(exact_mesh,repeated_combined).vertices==combined_vertices,"Combined GUI solve does not reproduce exactly");
                    double superposition_error{};
                    for(std::size_t i=0;i<combined_vertices.size();++i) {
                        const auto p=exact_mesh.surface.vertices[i],a=heat_result.displacement[i],b=mechanical_result.displacement[i],c=combined_vertices[i];
                        superposition_error=std::max(superposition_error,std::hypot(c.x-p.x-a.x-b.x,c.y-p.y-a.y-b.y,c.z-p.z-a.z-b.z));
                    }
                    std::cout<<"Combined GUI superposition maximum difference: "<<superposition_error<<" mm\n";
                    // Independently converged PCG right-hand sides need not
                    // stop on the same iteration. Bound their displacement
                    // difference to ten nanometres on this 120 mm fixture;
                    // the same combined input must still reproduce exactly.
                    check(superposition_error<1e-5,"GUI combined displacement violates superposition");
                    current_fem_action("femMeasurementPointAction")->trigger();flush();
                    measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);measure->findChild<QLineEdit*>("femConditionName")->setText("Probe B"+QString::number(mode));
                    measure->set_inline_parameter_value("placement:z",-20);
                    const auto second_id=measure->pending_value().id;measure->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                    current_fem_action("femMeasurementDistanceAction")->trigger();flush();
                    auto* distance=dynamic_cast<FemDistanceDialog*>(window.properties_dialog_);check(distance,"Distance command did not open");
                    const auto distance_name="Span "+QString::number(mode);distance->findChild<QLineEdit*>("femDistanceName")->setText(distance_name);
                    const auto distance_id=distance->pending.id;
                    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* point_row{};
                    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==second_id){point_row=*it;break;}
                    check(point_row,"Measurement point Tree row missing");tree->scrollToItem(point_row);flush();
                    const QPointF tree_position=tree->visualItemRect(point_row).center();
                    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                        QMouseEvent click(type,tree_position,tree->viewport()->mapToGlobal(tree_position.toPoint()),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(tree->viewport(),&click);
                    }flush();check(distance->pending.distance->point_ids[0]==second_id&&distance->active_input(),"Distance Tree input did not advance");
                    QPointF pick_pixel;bool located=false;
                    for(int y=5;y<view->height()&&!located;y+=3)for(int x=5;x<view->width()&&!located;x+=3) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(!candidates.empty()&&candidates.front().owner_id==probe_id){pick_pixel={double(x),double(y)};located=true;}
                    }
                    check(located,"Distance second point cannot be picked in View");
                    for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                        QMouseEvent click(type,pick_pixel,view->mapToGlobal(pick_pixel.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(view,&click);
                    }flush();check(distance->pending.distance->point_ids[1]==probe_id&&!distance->active_input(),"Distance View input failed");
                    const auto before_distance=pressure_session.revision();mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);flush();
                    check(!window.properties_dialog_&&pressure_session.revision()>before_distance,"Distance middle-button OK failed");
                    check(window.active_fem_deformation()==combined_result&&window.active_generated_fem_mesh()==combined_mesh,"Distance invalidated calculation");
                    const auto distance_marker=[&] {
                        const auto& markers=view->result_markers();const auto it=std::ranges::find_if(markers,[&](const auto& m){return m.label.startsWith(distance_name+":");});
                        check(it!=markers.end(),"Distance result marker missing");return *it;
                    };
                    const auto measured=distance_marker();check(measured.label.contains("Δ")&&measured.label.endsWith(" mm"),"Distance result values missing");
                    window.fem_magnification_=80;window.set_fem_animation(true);flush();check(distance_marker().label==measured.label,"Animation changed distance");window.set_fem_animation(false);
                    window.fem_animation_fraction_=1;window.update_fem_deformation();flush();
                    const auto no_op=pressure_session.revision();window.show_fem_distance_properties(distance_id);flush();
                    distance=dynamic_cast<FemDistanceDialog*>(window.properties_dialog_);distance->resize(730,520);flush();
                    auto* references=distance->findChild<QTableWidget*>("femDistanceReferences");
                    check(references->rowHeight(0)==references->rowHeight(1)&&references->height()==70,"Distance resizing stretched reference rows");
                    auto* eye=references->cellWidget(0,4)->findChild<QToolButton*>();check(eye&&eye->isEnabled(),"Distance inspection eye missing");
                    eye->click();flush();check(!distance->active_input()&&std::ranges::any_of(view->transient_context_mesh().edges,[](const auto& edge){return edge.color=="#00D1FF";}),"Independent distance inspection did not show azure geometry");
                    eye->click();flush();
                    distance->arm(0);mouse(QEvent::MouseButtonPress,Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,Qt::MiddleButton);flush();
                    check(!distance->active_input()&&distance->pending.distance->point_ids[0]==second_id,"Short middle click erased reference");
                    distance->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(pressure_session.revision()==no_op,"Unchanged distance OK created Undo");
                    window.show_fem_distance_properties(distance_id);flush();distance=dynamic_cast<FemDistanceDialog*>(window.properties_dialog_);
                    distance->arm(0);distance->accept_point(probe_id);distance->reject();flush();check(distance_marker().label==measured.label,"Distance Cancel changed references");
                    window.remove_fem_condition(second_id);flush();check(distance_marker().label.contains(QObject::tr("A measurement point reference is unavailable.")),"Lost distance point shows stale values");
                    check(pressure_session.undo(),"Distance point removal Undo failed");window.refresh_scene();flush();check(distance_marker().label==measured.label,"Distance Undo lost result");
                    check(pressure_session.redo(),"Distance point removal Redo failed");window.refresh_scene();flush();
                    check(distance_marker().label.contains(QObject::tr("A measurement point reference is unavailable.")),"Distance Redo lost broken reference state");
                    check(pressure_session.undo(),"Distance point restoration failed");window.refresh_scene();flush();
                    window.show_fem_distance_properties(distance_id);flush();distance=dynamic_cast<FemDistanceDialog*>(window.properties_dialog_);
                    distance->arm(0);distance->accept_point(probe_id);distance->arm(1);distance->accept_point(second_id);
                    distance->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(distance_marker().label==measured.label,"Replacing both distance references changed symmetric values");
                    check(pressure_session.undo(),"Distance replacement Undo failed");window.refresh_scene();flush();
                    check(pressure_session.redo(),"Distance replacement Redo failed");window.refresh_scene();flush();
                    if(mode==2) {
                        auto refined=*pressure_session.document().body_history.find(beam_fem);refined.fem->mesh.maximum_size_mm=3.25;
                        static_cast<void>(workspace::commit_fem(window.workspace_,workspace::prepare_fem_edit(pressure_session.document(),beam_fem),refined));
                        window.refresh_scene();window.rebuild_application_toolbar();flush();check(!window.active_fem_deformation(),"Distance kept stale solution after mesh change");
                        generate->trigger();flush();solve->trigger();flush();
                        check(window.active_fem_deformation()&&distance_marker().label.contains("Δ")&&!distance_marker().label.contains(QObject::tr("Measurement point is outside the FEM volume.")),"Distance did not rebind after remeshing");
                    }
                    const auto saved=directory/("fem-combined-"+std::to_string(mode)+".prtz");pressure_session.document().save(saved,pressure_session.calculated_boundaries());
                    check(document::PartDocument::load(saved).body_history==pressure_session.document().body_history,"Combined distance save/reopen failed");
                    if(mode==0) {
                        for(const auto* language:{"cs","en","de","fr","ru"}) {
                            auto settings=localized_settings(language);apply_application_translations(application,settings);window.rebuild_application_toolbar();flush();
                            check(current_fem_action("femMeasurementDistanceAction")->text()==settings.qt_translations.value("Distance measurement"),"Distance command is untranslated");
                            analysis_settings=body_settings();
                            auto* kind=analysis_settings->findChild<QComboBox*>("femAnalysisKind");
                            check(kind->currentText()==settings.qt_translations.value("Thermal-mechanical analysis"),"Combined analysis is untranslated");
                            analysis_settings->reject();flush();
                            window.show_fem_distance_properties(distance_id);flush();distance=dynamic_cast<FemDistanceDialog*>(window.properties_dialog_);
                            auto* table=distance->findChild<QTableWidget*>("femDistanceReferences");
                            check(table->item(0,2)->text()==settings.qt_translations.value("First point")&&table->item(1,2)->text()==settings.qt_translations.value("Second point"),"Distance references are untranslated");
                            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-distance-dialog-")+language+".png"))));
                            distance->reject();flush();
                        }
                        apply_application_translations(application,thermal_display_settings);window.refresh_scene();window.rebuild_application_toolbar();flush();
                    }
                    window.select_fem_result(AssemblyWorkspaceWindow::FemResultQuantity::Displacement);flush();
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("fem-combined-distance-"+std::to_string(mode)+".png"))));
                    // Restore the pure thermal loop's scope for the next patch.
                    window.remove_fem_condition(distance_id);window.remove_fem_condition(second_id);window.remove_fem_condition(force.id);flush();
                    static_cast<void>(workspace::commit_fem_condition(window.workspace_,workspace::prepare_fem_edit(pressure_session.document(),beam_fem),saved_pressure));
                    analysis_settings=body_settings();choose_thermal(analysis_settings);analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                    window.fem_magnification_=1;window.fem_animation_fraction_=1;
                    std::cout<<"Combined FEM GUI region "<<mode<<": heat/force/pressure superposition, distance View/Tree entry, cache, Cancel, Undo and native reopen passed\n";
                }
