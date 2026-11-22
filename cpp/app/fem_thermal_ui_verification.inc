            // A persistent inspection point samples the existing volume. It
            // must never acquire the meshing/load semantics of a point force.
            const auto inspection_calculations=kernel::body_calculation_count.load();
            const auto* existing_result=window.active_fem_deformation();
            const auto* existing_mesh=window.active_generated_fem_mesh();
            auto* measure_action=window.findChild<QAction*>("femMeasurementPointAction");
            check(measure_action&&measure_action->isEnabled(),"Separate FEM measurement point command is missing");
            measure_action->trigger();flush();
            auto* measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
            check(measure&&measure->pending_value().kind==document::FemConditionKind::MeasurementPoint&&
                !measure->findChild<QComboBox*>("femConditionKind")->isVisible()&&
                !measure->findChild<QDoubleSpinBox*>("femForceValue")->isVisible(),"Measurement point exposes load controls");
            measure->findChild<QLineEdit*>("femConditionName")->setText("Probe A");
            check(measure->set_inline_parameter_value("placement:z",12.345),"Measurement point position is not editable");
            const auto probe_id=measure->pending_value().id;
            const auto revision_before_probe=pressure_session.revision();
            measure->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!window.properties_dialog_,"Measurement point did not commit");
            check(pressure_session.revision()>revision_before_probe,"Measurement point did not create a transaction");
            check(window.active_generated_fem_mesh()==existing_mesh,"Creating a measurement point invalidated the mesh");
            check(existing_result&&window.active_fem_deformation()==existing_result,"Creating a measurement point invalidated the result");
            const auto measurement_marker=[&]() -> viewer::MeshView::ResultMarker {
                const auto& markers=view->result_markers();const auto found=std::ranges::find_if(markers,[](const auto& m){return m.label.startsWith("Probe A:");});
                check(found!=markers.end(),"Measurement result marker is missing");return *found;
            };
            check(measurement_marker().label.contains("ΔX")&&measurement_marker().label.contains("ΔY")&&measurement_marker().label.contains("ΔZ"),
                "Measurement did not report displacement components");
            for(double z:{-10.25,0.,15.125}) {
                window.show_fem_condition_properties(document::FemConditionKind::MeasurementPoint,probe_id);flush();
                measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                check(measure&&measure->set_inline_parameter_value("placement:z",z),"Measurement point did not reopen for position editing");
                measure->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(window.active_fem_deformation()==existing_result&&window.active_generated_fem_mesh()==existing_mesh,
                    "Moving a measurement point requires a new calculation");
                const auto m=measurement_marker();
                check(std::abs(m.position.z-m.displacement.z-z)<1e-7&&std::abs(m.position.x-m.displacement.x)<1e-7,
                    "Measurement marker lost its original position");
            }
            const auto final_probe=measurement_marker();
            window.fem_magnification_=37;window.fem_animation_fraction_=.4;window.update_fem_deformation();flush();
            check(measurement_marker().label==final_probe.label,"Visual magnification changed measured values");
            window.fem_magnification_=window.fem_animation_fraction_=1;window.update_fem_deformation();
            window.show_fem_condition_properties(document::FemConditionKind::MeasurementPoint,probe_id);flush();
            measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
            check(measure->set_inline_parameter_value("placement:z",1000),"Exterior measurement position could not be entered");
            measure->reject();flush();check(measurement_marker().label==final_probe.label,"Measurement Cancel changed its result");
            window.show_fem_condition_properties(document::FemConditionKind::MeasurementPoint,probe_id);flush();
            measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);measure->set_inline_parameter_value("placement:z",1000);
            measure->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(measurement_marker().label.contains(QObject::tr("Measurement point is outside the FEM volume.")),"Exterior point was silently snapped to a node");
            check(pressure_session.undo(),"Measurement Undo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
            check(measurement_marker().label==final_probe.label,"Measurement Undo changed its interpolation");
            check(pressure_session.redo(),"Measurement Redo failed");window.refresh_scene();flush();
            check(measurement_marker().label.contains(QObject::tr("Measurement point is outside the FEM volume.")),"Measurement Redo did not restore exterior state");
            check(pressure_session.undo(),"Measurement restore failed");window.refresh_scene();flush();
            window.show_fem_condition_properties(document::FemConditionKind::MeasurementPoint,probe_id);flush();
            measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);const auto no_op=pressure_session.revision();
            measure->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(pressure_session.revision()==no_op,"Unchanged measurement OK created Undo");
            const auto mechanical=pressure_session.document();
            // Whole-Origin placement uses the actual shared GUI action, and
            // middle-button double-click commits this independent command.
            window.findChild<QAction*>("femMeasurementPointAction")->trigger();flush();
            measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
            auto* measure_origin=measure->findChild<QPushButton*>("containerDefaultOriginButton");
            check(measure_origin&&measure_origin->isEnabled(),"Measurement point cannot adopt an Origin");
            measure_origin->click();flush();
            const auto origin_probe=measure->pending_value();
            check(document::placement_references_use_whole_origin(origin_probe.placement.references),"Measurement point did not adopt its whole Origin");
            mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);flush();
            check(!window.properties_dialog_&&window.active_fem_condition(origin_probe.id),"Measurement middle-button confirmation failed");
            window.show_fem_condition_properties(document::FemConditionKind::MeasurementPoint,origin_probe.id);flush();
            measure=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
            check(measure->pending_value().placement.references==origin_probe.placement.references,"Measurement Origin references did not reopen");
            measure->reject();flush();window.remove_fem_condition(origin_probe.id);flush();
            const auto probe_path=directory/"fem-measurement.prtz";
            mechanical.save(probe_path,pressure_session.calculated_boundaries());
            check(document::PartDocument::load(probe_path).body_history==mechanical.body_history,"Measurement save/reopen lost its placement");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"fem-measurement.png")));
            check(kernel::body_calculation_count.load()==inspection_calculations,"Measurement edited CAD geometry");

            // Use the material already owned by the Part, with explicit units.
            auto thermal_material=workspace::material_data(window.workspace_,beam.document_id);
            thermal_material.properties["THERMAL_CONDUCTIVITY"]="50";thermal_material.units["THERMAL_CONDUCTIVITY"]="W/(m*K)";
            thermal_material.properties["THERMAL_EXPANSION_COEFFICIENT"]="1.2e-5";thermal_material.units["THERMAL_EXPANSION_COEFFICIENT"]="1/C";
            thermal_material.properties["THERM_EXPANSION_REF_TEMPERATURE"]="25";thermal_material.units["THERM_EXPANSION_REF_TEMPERATURE"]="C";
            static_cast<void>(workspace::set_material_data(window.workspace_,beam.document_id,thermal_material));
            const auto body_settings=[&] {window.show_fem_body_properties(beam_fem);flush();return dynamic_cast<FemBodyDialog*>(window.properties_dialog_);};
            auto* analysis_settings=body_settings();check(analysis_settings,"FEM analysis settings missing");
            const auto choose_thermal=[&](FemBodyDialog* form) {
                auto* type=form->findChild<QComboBox*>("femAnalysisKind");type->setCurrentIndex(type->findData(int(document::FemAnalysisKind::SteadyStateThermal)));flush();
                check(form->findChild<QDoubleSpinBox*>("femAmbientTemperature")->isVisible(),"Thermal settings remained hidden");
                form->findChild<QDoubleSpinBox*>("femAmbientTemperature")->setValue(25);
                form->findChild<QDoubleSpinBox*>("femHeatTransfer")->setValue(10);
            };
            choose_thermal(analysis_settings);analysis_settings->reject();flush();check(!window.fem_thermal_analysis(),"Analysis Cancel changed solver kind");
            analysis_settings=body_settings();choose_thermal(analysis_settings);analysis_settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(window.fem_thermal_analysis()&&!window.active_generated_fem_mesh()&&!window.active_fem_deformation(),
                "Analysis switch retained mesh regions for the previous load type");
            check(pressure_session.undo(),"Analysis settings Undo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
            check(!window.fem_thermal_analysis(),"Analysis Undo retained thermal mode");check(pressure_session.redo(),"Analysis Redo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
            const auto current_fem_action=[&](const char* name) {
                for(auto* action:window.tools_toolbar_->actions())if(action->objectName()==name)return action;
                throw std::runtime_error(std::string("Visible FEM action missing: ")+name);
            };
            for(const auto* language:{"cs","en","de","fr","ru"}) {
                auto localized=localized_settings(language);apply_application_translations(application,localized);window.rebuild_application_toolbar();flush();
                if(current_fem_action("femHeatSourceAction")->text()!=localized.qt_translations.value("Heat source")||
                    current_fem_action("femMeasurementPointAction")->text()!=localized.qt_translations.value("Measurement point"))
                    throw std::runtime_error(std::string("New FEM commands did not translate: ")+language+" actual="+
                        current_fem_action("femHeatSourceAction")->text().toStdString()+"/"+
                        current_fem_action("femMeasurementPointAction")->text().toStdString()+" expected="+
                        localized.qt_translations.value("Heat source").toStdString()+"/"+localized.qt_translations.value("Measurement point").toStdString());
                for(const auto* theme:{"dark","light"}) {
                    localized.theme=theme;apply_application_appearance(application,localized);
                    analysis_settings=body_settings();
                    for(const auto* key:{"Ambient temperature","Heat transfer coefficient"})
                        check(std::ranges::any_of(analysis_settings->findChildren<QLabel*>(),[&](const auto* label){return label->text()==localized.qt_translations.value(key);}),"Thermal dialog label is untranslated");
                    auto* expansion_box=analysis_settings->findChild<QCheckBox*>("femThermalExpansion");
                    check(expansion_box&&expansion_box->text()==localized.qt_translations.value("Calculate thermal expansion"),"Expansion control is untranslated");
                    expansion_box->setChecked(true);flush();
                    check(std::ranges::any_of(analysis_settings->findChildren<QLabel*>(),[&](const auto* label){return label->isVisible()&&label->text()==localized.qt_translations.value(
                        "Uses the Part material's expansion coefficient and reference temperature, and mechanical restraints. Forces and pressure are excluded. Measurement points report temperature and displacement caused by heating.");}),"Expansion explanation is untranslated");
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-thermal-settings-")+language+"-"+theme+".png"))));
                    analysis_settings->reject();flush();
                    current_fem_action("femHeatSourceAction")->trigger();flush();
                    auto* heat_form=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                    check(heat_form&&std::ranges::any_of(heat_form->findChildren<QLabel*>(),[&](const auto* label){
                        return label->text()==localized.qt_translations.value("Thermal power");}),"Heat power label did not translate");
                    check(heat_form->findChild<QDoubleSpinBox*>("femHeatPower")->toolTip()==localized.qt_translations.value(
                        "Total power over the selected area. Positive adds heat; negative removes heat."),"Heat power tooltip did not translate");
                    heat_form->reject();flush();
                }
            }
            auto thermal_display_settings=localized_settings("cs");thermal_display_settings.theme="dark";
            apply_application_translations(application,thermal_display_settings);apply_application_appearance(application,thermal_display_settings);
            window.rebuild_application_toolbar();flush();
            for(int mode=0;mode<3;++mode) {
                current_fem_action("femHeatSourceAction")->trigger();flush();
                auto* heat=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                check(heat&&heat->pending_value().kind==document::FemConditionKind::HeatSource&&heat->pending_value().region==document::FemRegionKind::Surface,
                    "Heat source shortcut did not select its area workflow");
                heat->findChild<QDoubleSpinBox*>("femHeatPower")->setValue(2);
                if(mode==0) {const auto face=cap_at(span/2);heat->set_geometry(0,{face.owner_id,face.semantic_key,face.instance_path});}
                else {
                    auto* region=heat->findChild<QComboBox*>("femConditionRegion");region->setCurrentIndex(region->findData(int(document::FemRegionKind::Surface)));
                    heat->findChild<QCheckBox*>("femProjectSketch")->setChecked(mode==2);
                    heat->findChild<QDoubleSpinBox*>("femSketchOffset")->setValue(span/2+(mode==2?5:0));
                    auto sketch=sketcher::Sketch::from_serialized(heat->pending_value().projected_sketch->serialized);
                    static_cast<void>(sketch.add_rectangle(-3,-2,3,2));heat->set_owned_sketch(sketch);
                    if(mode==2){const auto face=cap_at(span/2);heat->set_geometry(0,{face.owner_id,face.semantic_key,face.instance_path},kernel::Vec3{0,0,span/2});}
                }
                const auto heat_id=heat->pending_value().id;
                heat->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!window.properties_dialog_,"Heat source did not commit");
                generate->trigger();flush();if(!window.active_generated_fem_mesh())
                    throw std::runtime_error("Thermal mesh command failed in region "+std::to_string(mode)+": "+window.state_->text().toStdString());
                const auto thermal_revision=pressure_session.revision();solve->trigger();flush();
                if(!window.active_fem_deformation())throw std::runtime_error("Thermal GUI solve failed: "+window.state_->text().toStdString());
                check(window.fem_result_quantity_==AssemblyWorkspaceWindow::FemResultQuantity::Temperature&&
                    !window.findChild<QAction*>("femAnimateAction")->isVisible()&&
                    !window.findChild<QAction*>("femDisplacementResultAction")->isVisible(),"Thermal results expose mechanical deformation controls");
                check(window.active_fem_deformation()->vertices==window.active_generated_fem_mesh()->vertices&&
                    !view->triangle_scalar_field().empty()&&measurement_marker().label.endsWith(" °C"),"Thermal result or measurement temperature missing");
                check(pressure_session.revision()==thermal_revision,"Thermal solve edited the document");
                const auto first_temperature=view->triangle_scalar_field();const auto* thermal_mesh=window.active_generated_fem_mesh();
                const auto* result_before_repeat=window.active_fem_deformation();solve->trigger();flush();
                check(window.active_fem_deformation()==result_before_repeat,"Unchanged thermal solve discarded cached results");
                window.show_fem_condition_properties(document::FemConditionKind::HeatSource,heat_id);flush();
                heat=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);heat->findChild<QDoubleSpinBox*>("femHeatPower")->setValue(4);
                heat->reject();flush();check(view->triangle_scalar_field()==first_temperature,"Heat source Cancel changed results");
                window.show_fem_condition_properties(document::FemConditionKind::HeatSource,heat_id);flush();
                heat=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);heat->findChild<QDoubleSpinBox*>("femHeatPower")->setValue(4);
                heat->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(window.active_generated_fem_mesh()==thermal_mesh&&!window.active_fem_deformation(),"Heat value edit remeshed or kept stale results");
                solve->trigger();flush();const auto second=view->triangle_scalar_field();
                check(second.size()==first_temperature.size(),"Thermal load edit lost surface cells");
                for(std::size_t i=0;i<second.size();++i)check(std::abs((second[i]-25)-2*(first_temperature[i]-25))<1e-5,"Thermal command violates linear heat balance");
                check(pressure_session.undo(),"Heat power Undo failed");window.refresh_scene();window.rebuild_application_toolbar();solve->trigger();flush();
                check(view->triangle_scalar_field()==first_temperature,"Heat Undo did not restore temperatures");
                check(pressure_session.redo(),"Heat power Redo failed");window.refresh_scene();window.rebuild_application_toolbar();solve->trigger();flush();
                check(view->triangle_scalar_field()==second,"Heat Redo did not restore temperatures");
                const auto thermal_path=directory/("fem-thermal-"+std::to_string(mode)+".prtz");
                pressure_session.document().save(thermal_path,pressure_session.calculated_boundaries());
                const auto restored=document::PartDocument::load(thermal_path);
                check(restored.body_history==pressure_session.document().body_history,"Thermal save/reopen lost settings, sources or measurement points");
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("fem-temperature-"+std::to_string(mode)+".png"))));
#include "fem_expansion_ui_verification.inc"
                window.remove_fem_condition(heat_id);flush();
            }
            check(kernel::body_calculation_count.load()==inspection_calculations,"Thermal analysis recalculated CAD");
            std::cout<<"FEM thermal/measurement GUI: separate commands, cached point edits, components, temperature, body/planar/projected areas, settings, units, Cancel, Undo/Redo, native reopen and five languages/two themes passed\n";
