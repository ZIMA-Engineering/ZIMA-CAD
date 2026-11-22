                analysis_settings=body_settings();
                analysis_settings->findChild<QCheckBox*>("femThermalExpansion")->setChecked(true);
                analysis_settings->reject();flush();check(!window.fem_expansion_analysis(),"Expansion Cancel committed settings");
                analysis_settings=body_settings();analysis_settings->findChild<QCheckBox*>("femThermalExpansion")->setChecked(true);
                analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(window.fem_expansion_analysis()&&!window.active_generated_fem_mesh(),"Expansion did not include restraint regions");
                check(pressure_session.undo(),"Expansion Undo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
                check(!window.fem_expansion_analysis(),"Expansion Undo retained setting");
                check(pressure_session.redo(),"Expansion Redo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
                current_fem_action("femConstraintAction")->trigger();flush();
                auto* thermal_support=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                check(thermal_support&&!thermal_support->findChild<QComboBox*>("femConditionKind")->isVisible(),"Thermal restraint offers mechanical loads");
                thermal_support->reject();flush();
                generate->trigger();flush();solve->trigger();flush();
                if(!window.active_fem_deformation())throw std::runtime_error("Thermal expansion GUI solve failed: "+window.state_->text().toStdString());
                check(window.findChild<QAction*>("femTemperatureResultAction")->isVisible()&&
                    window.findChild<QAction*>("femDisplacementResultAction")->isVisible()&&
                    window.findChild<QAction*>("femStressResultAction")->isVisible()&&
                    window.findChild<QAction*>("femAnimateAction")->isVisible(),"Coupled result commands are missing");
                const auto coupled_measurement=measurement_marker();
                check(coupled_measurement.label.contains("°C")&&coupled_measurement.label.contains("ΔX")&&coupled_measurement.label.contains("ΔZ")&&
                    std::hypot(coupled_measurement.displacement.x,coupled_measurement.displacement.y,coupled_measurement.displacement.z)>1e-5,
                    "Measurement point lacks temperature and heating displacement");
                const auto* coupled_mesh=window.active_generated_fem_mesh();const auto* coupled_result=window.active_fem_deformation();
                const auto coupled_vertices=coupled_result->vertices;const auto coupled_temperature=view->triangle_scalar_field();
                const auto coupled_revision=pressure_session.revision();
                analysis_settings=body_settings();analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(pressure_session.revision()==coupled_revision&&window.active_fem_deformation()==coupled_result,"Unchanged expansion OK recalculated or created Undo");
                for(auto quantity:{AssemblyWorkspaceWindow::FemResultQuantity::Displacement,AssemblyWorkspaceWindow::FemResultQuantity::VonMises,AssemblyWorkspaceWindow::FemResultQuantity::Temperature}) {
                    window.select_fem_result(quantity);flush();
                    check(window.fem_result_quantity_==quantity&&!view->triangle_scalar_field().empty()&&measurement_marker().label==coupled_measurement.label,
                        "Coupled result selection changed measurements or lost colors");
                }
                window.fem_magnification_=100;window.fem_show_original_=true;window.set_fem_animation(true);flush();
                check(window.fem_animation_running()&&measurement_marker().label==coupled_measurement.label,"Thermal animation changed measured values");
                window.set_fem_animation(false);window.fem_animation_fraction_=1;window.update_fem_deformation();flush();
                const auto coupled_path=directory/("fem-expansion-"+std::to_string(mode)+".prtz");
                pressure_session.document().save(coupled_path,pressure_session.calculated_boundaries());
                const auto coupled_document=document::PartDocument::load(coupled_path);
                check(coupled_document.body_history==pressure_session.document().body_history,"Thermal expansion save/reopen lost its definition");
                const auto& coupled_definition=*coupled_document.body_history.find(beam_fem)->fem;
                const auto reproduced_mesh=generate_fem_volume_mesh(solid.get(),coupled_definition);
                const auto reproduced_heat=solve_fem_steady_thermal(reproduced_mesh,coupled_definition,fem_thermal_material(thermal_material));
                const FemThermalStrain reproduced_strain{fem_expansion_material(thermal_material),reproduced_heat.temperature_C};
                const auto reproduced=solve_fem_linear_statics(reproduced_mesh,coupled_definition,{young,poisson},{},&reproduced_strain);
                check(fem_deformed_mesh(reproduced_mesh,reproduced).vertices==coupled_vertices,"Saved thermal expansion does not reproduce GUI displacement");
                window.select_fem_result(AssemblyWorkspaceWindow::FemResultQuantity::Displacement);flush();
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("fem-expansion-displacement-"+std::to_string(mode)+".png"))));
                if(mode==2) {
                    auto doubled_material=thermal_material;doubled_material.properties["THERMAL_EXPANSION_COEFFICIENT"]="2.4e-5";
                    static_cast<void>(workspace::set_material_data(window.workspace_,beam.document_id,doubled_material));window.refresh_scene();window.rebuild_application_toolbar();flush();
                    check(window.active_generated_fem_mesh()==coupled_mesh&&!window.active_fem_deformation(),"Expansion material edit retained stale results or remeshed");
                    solve->trigger();flush();check(window.active_fem_deformation(),"Edited expansion coefficient did not solve");
                    const auto doubled_measurement=measurement_marker();
                    check(std::hypot(doubled_measurement.displacement.x-2*coupled_measurement.displacement.x,
                        doubled_measurement.displacement.y-2*coupled_measurement.displacement.y,
                        doubled_measurement.displacement.z-2*coupled_measurement.displacement.z)<1e-8,"Doubling expansion coefficient did not double displacement");
                    window.select_fem_result(AssemblyWorkspaceWindow::FemResultQuantity::Temperature);flush();
                    check(view->triangle_scalar_field()==coupled_temperature,"Expansion coefficient changed temperature");
                    check(pressure_session.undo(),"Expansion material Undo failed");window.refresh_scene();window.rebuild_application_toolbar();solve->trigger();flush();
                    check(window.active_fem_deformation()->vertices==coupled_vertices,"Expansion material Undo changed solution");
                    auto invalid_material=thermal_material;invalid_material.properties.erase("THERMAL_EXPANSION_COEFFICIENT");
                    invalid_material.units.erase("THERMAL_EXPANSION_COEFFICIENT");invalid_material.descriptions.erase("THERMAL_EXPANSION_COEFFICIENT");
                    static_cast<void>(workspace::set_material_data(window.workspace_,beam.document_id,invalid_material));window.refresh_scene();window.rebuild_application_toolbar();solve->trigger();flush();
                    check(!window.active_fem_deformation()&&window.state_->text()==QObject::tr("Set a valid thermal expansion coefficient and reference temperature in the Part material."),"Missing expansion material was accepted");
                    check(pressure_session.undo(),"Expansion invalid material restoration failed");window.refresh_scene();window.rebuild_application_toolbar();solve->trigger();flush();
                    auto refined_edit=workspace::prepare_fem_edit(pressure_session.document(),beam_fem);
                    auto refined_body=*pressure_session.document().body_history.find(beam_fem);
                    refined_body.fem->mesh.maximum_size_mm=3.5;static_cast<void>(workspace::commit_fem(window.workspace_,refined_edit,refined_body));
                    window.refresh_scene();window.rebuild_application_toolbar();flush();check(!window.active_generated_fem_mesh()&&!window.active_fem_deformation(),"Mesh edit retained expansion results");
                    generate->trigger();flush();solve->trigger();flush();check(window.active_fem_deformation()&&measurement_marker().label.contains("ΔX"),"Measurement failed after remeshing");
                }
                window.fem_magnification_=1;window.fem_show_original_=false;
                analysis_settings=body_settings();analysis_settings->findChild<QCheckBox*>("femThermalExpansion")->setChecked(false);
                analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!window.fem_expansion_analysis()&&!window.active_fem_deformation(),"Disabling expansion retained mechanical results");
                std::cout<<"FEM thermal expansion GUI region "<<mode<<": paired measurement, restraints, result selection, animation, settings, save/reopen and cache checks passed\n";
