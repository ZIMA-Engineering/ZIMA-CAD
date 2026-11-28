#include "../fem_condition_labels.hpp"
#include "workspace_internal.hpp"
#include <zima/workspace/shell_operations.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include "../command_button_paint.hpp"
#include "../documentation.hpp"

namespace zima::app {
using namespace workspace_detail;

namespace {


} // namespace


void AssemblyWorkspaceWindow::update_document_area_visibility() {
    refresh_named_view_choices();
    const bool web=active_browser_!=nullptr;
    const bool files=active_file_manager_!=nullptr;const bool preview=active_preview_!=nullptr;
    const bool render=active_render_page_!=nullptr;
    const bool has_document = workspace_.size() != 0 && !web && !files && !preview && !render;
    if(render_stack_)render_stack_->setVisible(render);
    if (tabs_ != nullptr) tabs_->setVisible(workspace_.size()!=0||!browser_tabs_.empty()||!file_manager_tabs_.empty()||!preview_tabs_.empty());
    if(preview_stack_)preview_stack_->setVisible(preview);
    if(browser_stack_)browser_stack_->setVisible(web);
    if(file_manager_stack_)file_manager_stack_->setVisible(files);
    if (document_splitter_ != nullptr) document_splitter_->setVisible(has_document);
    if (centralWidget() != nullptr) centralWidget()->update();
    save_action_->setEnabled(has_document||render);
    save_as_action_->setEnabled(has_document||render);
    close_document_action_->setEnabled(has_document||web||files||preview||render);
    regenerate_document_action_->setVisible(has_document);
    regenerate_document_action_->setEnabled(has_document);
    refresh_file_manager_action_->setVisible(files);
    refresh_file_manager_action_->setEnabled(files);
    fit_view_action_->setEnabled(has_document);
    selection_action_->setEnabled(has_document);
    selection_filter_combo_->setEnabled(has_document &&
        !workspace_.open_drawing(workspace_.active_document_id()));
    import_action_->setEnabled(has_document);
    export_action_->setEnabled(has_document||render);
    parameters_action_->setEnabled(has_document);
    const bool has_editable_model = has_document &&
        (workspace_.open_part(workspace_.active_document_id()) != nullptr ||
         workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    material_action_->setEnabled(has_document&&workspace_.open_part(workspace_.active_document_id()) != nullptr);
    relations_action_->setEnabled(has_editable_model);
    family_table_action_->setEnabled(relations_action_->isEnabled());
    file_settings_action_->setEnabled(has_editable_model);
    standard_views_menu_->menuAction()->setEnabled(has_document);
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_,
                         orthographic_camera_action_, perspective_camera_action_,
                         fly_camera_action_, show_origins_action_, show_points_action_,
                         show_axes_action_, show_planes_action_, show_surfaces_action_, show_sketches_action_, show_dimensions_action_, show_symbols_action_}) {
        action->setEnabled(has_document);
    }
    update_application_actions();
    if(command_insert_menu_)command_insert_menu_->setEnabled(has_document);
    if(undo_action_)undo_action_->setEnabled((has_document||render)&&undo_action_->isEnabled());
    if(redo_action_)redo_action_->setEnabled((has_document||render)&&redo_action_->isEnabled());
    if(render)if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
        undo_action_->setEnabled(part->session.can_undo());redo_action_->setEnabled(part->session.can_redo());
    }
    if(section_action_&&(web||files||preview||render))section_action_->setEnabled(false);
    if(measure_action_&&(web||files||preview||render))measure_action_->setEnabled(false);
    refresh_delete_file_actions();
    if(preview)for(auto* action:{rename_document_action_,delete_current_file_action_})action->setEnabled(false);
    if (!has_document && state_ != nullptr) state_->setText(tr("Připraveno."));
}

void AssemblyWorkspaceWindow::update_application_actions() {
    if(section_action_)section_action_->setText(tr("Create section"));
    if(section_action_)section_action_->setEnabled(workspace_.active_document_id()==workspace_.displayed_document_id()&&!workspace_.open_drawing(workspace_.displayed_document_id())&&!properties_dialog_&&active_sketch_id_.empty()&&!template_sketch());
    for (auto* action : application_actions_) action->setEnabled(false);
    if(application_mode_selector_)application_mode_selector_->hide();
    if (workspace_.size() == 0 || active_browser_ || active_file_manager_ || active_preview_ || active_render_page_) return;
    const auto owner=workspace_.active_document_id();
    const auto saved=document_application_modes_.find(owner);
    active_application_=saved==document_application_modes_.end()
        ? (workspace_.open_assembly(owner)?ApplicationMode::Assembly:ApplicationMode::Modeling) : saved->second;
    if (workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr) {
        application_actions_[static_cast<std::size_t>(ApplicationMode::Drawing)]
            ->setEnabled(true);
        active_application_ = ApplicationMode::Drawing;
    } else if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        const bool active_body = !part->session.document().body_history.active_body_id().empty();
        const bool fem=active_fem_body(),render=active_render_body(),cam=active_cam_body();
        application_actions_[static_cast<std::size_t>(ApplicationMode::Modeling)]->setEnabled(!fem&&!render&&!cam);
        application_actions_[static_cast<std::size_t>(ApplicationMode::Fem)]->setEnabled(fem);
        application_actions_[static_cast<std::size_t>(ApplicationMode::Render)]->setEnabled(render);
        application_actions_[static_cast<std::size_t>(ApplicationMode::Cam)]->setEnabled(cam);
        if(cam)active_application_=ApplicationMode::Cam;
        else if(render)active_application_=ApplicationMode::Render;
        else if(fem)active_application_=ApplicationMode::Fem;
        else if (active_body) {
            const auto* body=part->session.document().body_history.find(part->session.document().body_history.active_body_id());
            application_actions_[static_cast<std::size_t>(ApplicationMode::Mech)]->setEnabled(body&&!body->derived_copy&&!body->suppressed);
            application_actions_[static_cast<std::size_t>(ApplicationMode::SheetMetal)]
                ->setEnabled(true);
            application_actions_[static_cast<std::size_t>(ApplicationMode::Surface)]
                ->setEnabled(true);
        } else
            active_application_ = ApplicationMode::Modeling;
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Modeling;
        }
    } else if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        application_actions_[static_cast<std::size_t>(ApplicationMode::Assembly)]
            ->setEnabled(true);
        application_actions_[static_cast<std::size_t>(ApplicationMode::Mechanisms)]
            ->setEnabled(true);
        if(active_application_!=ApplicationMode::Mechanisms)
            active_application_ = ApplicationMode::Assembly;
    }
    application_actions_[static_cast<std::size_t>(active_application_)]->setChecked(true);
    if(application_mode_selector_) {
        const auto* part=workspace_.open_part(workspace_.active_document_id());
        const bool part_context=part&&
            !part->session.document().body_history.active_body_id().empty();
        const bool fem=active_fem_body(),render=active_render_body(),cam=active_cam_body();
        if(modeling_mode_button_)
            modeling_mode_button_->setVisible(part_context&&!fem&&!render&&!cam);
        if(fem_mode_button_)fem_mode_button_->setVisible(fem);
        if(render_mode_button_)render_mode_button_->setVisible(render);
        if(cam_mode_button_)cam_mode_button_->setVisible(cam);
        const bool assembly_context=workspace_.open_assembly(workspace_.active_document_id())!=nullptr;
        if(assembly_mode_button_)assembly_mode_button_->setVisible(assembly_context);
        if(mechanisms_mode_button_)mechanisms_mode_button_->setVisible(assembly_context);
        if(sheet_metal_mode_button_)
            sheet_metal_mode_button_->setVisible(part_context&&!fem&&!render&&!cam);
        if(surface_mode_button_)
            surface_mode_button_->setVisible(part_context&&!fem&&!render&&!cam);
        if(mech_mode_button_)mech_mode_button_->setVisible(part_context&&!fem&&!render&&!cam);
        application_mode_selector_->setVisible(
            (part_context||assembly_context)&&
            !workspace_.open_drawing(workspace_.displayed_document_id()));
    }
    if(properties_dialog_ || !active_sketch_id_.empty() || section_dialog_)
        for(auto* action:application_actions_)action->setEnabled(false);
    update_assembly_group_states();
}

void AssemblyWorkspaceWindow::set_active_application(ApplicationMode mode) {
    const auto index = static_cast<std::size_t>(mode);
    if (index >= application_actions_.size() ||
        !application_actions_[index]->isEnabled()) {
        update_application_actions();
        return;
    }
    active_application_ = mode;
    document_application_modes_[workspace_.active_document_id()]=mode;
    application_actions_[index]->setChecked(true);
    rebuild_application_toolbar();
    update_assembly_group_states();
    // Ordinary priorities read the active workspace. Retire an unconfirmed
    // hover/cycling list while preserving an existing View/Tree selection.
    if(viewer_&&!viewer_->confirmed_candidate())viewer_->set_candidate_filter(viewer_->candidate_filter(),
        viewer_->advances_selection_on_hover());
}

void AssemblyWorkspaceWindow::activate_assembly_group(QTreeWidgetItem* item) {
    if(!item||properties_dialog_||tree_edit_dialog_||section_dialog_||!active_sketch_id_.empty())return;
    const auto role=item->data(0,Qt::UserRole+3).toString();
    if(role!="assembly-model-group"&&role!="assembly-mechanisms-group")return;
    // Activation can rebuild the Tree, so retain values rather than its item.
    const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
    const auto owner=item->data(0,Qt::UserRole+2).toString().toStdString();
    if(workspace_.active_document_id()!=owner||workspace_.active_occurrence_path()!=path) {
        if(path.empty())deactivate_active_occurrence_for_test();
        else if(!activate_occurrence_for_test(path))return;
    }
    if(workspace_.active_document_id()!=owner||workspace_.active_occurrence_path()!=path)return;
    set_active_application(role=="assembly-mechanisms-group"?ApplicationMode::Mechanisms:ApplicationMode::Assembly);
}

void AssemblyWorkspaceWindow::update_assembly_group_states() {
    if(!tree_)return;
    const QSignalBlocker block(tree_);
    for(QTreeWidgetItemIterator it(tree_);*it;++it) {
        auto* item=*it;
        const auto role=item->data(0,Qt::UserRole+3).toString();
        if(role!="assembly-model-group"&&role!="assembly-mechanisms-group")continue;
        const bool mechanisms=role=="assembly-mechanisms-group";
        const bool active=item->data(0,Qt::UserRole+2).toString().toStdString()==workspace_.active_document_id()&&
            item->data(0,Qt::UserRole+1).toString().toStdString()==workspace_.active_occurrence_path()&&
            active_application_==(mechanisms?ApplicationMode::Mechanisms:ApplicationMode::Assembly);
        item->setBackground(0,active?QBrush(QColor("#4DD811")):QBrush{});
        item->setForeground(0,active?QBrush(QColor("#102027")):QBrush{});
        auto font=item->font(0);font.setBold(active);item->setFont(0,font);
    }
}

void AssemblyWorkspaceWindow::rebuild_application_toolbar() {
    if (tools_toolbar_ == nullptr) return;
    if(fem_results_menu_) {
        fem_results_menu_->setTitle(tr("Results"));
        const bool available=active_fem_body()&&!properties_dialog_&&active_sketch_id_.empty()&&active_fem_deformation();
        const bool thermal=fem_thermal_analysis();
        const bool mechanical=!thermal||fem_expansion_analysis();
        fem_results_menu_->setEnabled(available);
        for(auto* action:fem_results_menu_->actions())action->setEnabled(available);
        auto* displacement=fem_results_menu_->findChild<QAction*>("femDisplacementResultAction");
        auto* stress=fem_results_menu_->findChild<QAction*>("femStressResultAction");
        auto* temperature=fem_results_menu_->findChild<QAction*>("femTemperatureResultAction");
        temperature->setText(tr("Temperature [°C]"));temperature->setVisible(thermal);
        temperature->setChecked(available&&show_fem_deformation_&&fem_result_quantity_==FemResultQuantity::Temperature);
        displacement->setVisible(mechanical);stress->setVisible(mechanical);
        for(const auto* name:{"femOriginalAction","femDeformationSettingsAction"})
            fem_results_menu_->findChild<QAction*>(name)->setVisible(mechanical);
        displacement->setText(tr("Displacement [mm]"));stress->setText(tr("Von Mises stress [MPa]"));
        stress->setToolTip(tr("Volume-averaged stress in each element, without nodal smoothing."));
        displacement->setChecked(available&&show_fem_deformation_&&fem_result_quantity_==FemResultQuantity::Displacement);
        stress->setChecked(available&&show_fem_deformation_&&fem_result_quantity_==FemResultQuantity::VonMises);
        if(!available||!show_fem_deformation_||!mechanical)set_fem_animation(false);
        const auto result_action=[&](const char* name,const QString& label,bool checked) {
            auto* action=fem_results_menu_->findChild<QAction*>(name);action->setText(label);action->setChecked(checked);return action;
        };
        result_action("femOriginalAction",tr("Transparent original"),fem_show_original_);
        result_action("femExtremaAction",tr("Show minimum / maximum"),fem_show_extrema_);
        result_action("femProbeAction",tr("Probe results"),fem_probe_enabled_)->setToolTip(
            fem_result_quantity_==FemResultQuantity::Temperature?tr("Click the displayed mesh to read the nearest surface node temperature."):
            tr("Click the displayed mesh to read the nearest surface node displacement or the owning element's average stress."));
        fem_results_menu_->findChild<QAction*>("femDeformationSettingsAction")->setText(tr("Deformation player"));
    }
    if(fem_mesh_menu_) {
        fem_mesh_menu_->setTitle(tr("Mesh"));
        const bool fem_active=active_fem_body()&&!active_browser_&&!active_file_manager_&&!active_preview_&&
            !workspace_.open_drawing(workspace_.displayed_document_id());
        fem_mesh_menu_->menuAction()->setVisible(fem_active);
        fem_mesh_menu_->setEnabled(fem_active&&!properties_dialog_);
        for(auto* action:fem_mesh_menu_->actions())action->setEnabled(fem_active&&!properties_dialog_);
        fem_mesh_menu_->findChild<QAction*>("femMeshSettingsAction")->setText(tr("Mesh settings"));
        if(auto* generate=fem_mesh_menu_->findChild<QAction*>("femGenerateMeshAction"))generate->setText(tr("Generate mesh"));
        if(auto* show=fem_mesh_menu_->findChild<QAction*>("femShowMeshAction")) {
            show->setText(tr("Show mesh"));
            const bool available=fem_active&&active_generated_fem_mesh();
            show->setEnabled(available&&!properties_dialog_);
            show->setChecked(available&&show_generated_fem_mesh_&&
                !(show_fem_deformation_&&active_fem_deformation()));
        }
        if(fem_solution_menu_) {
            fem_solution_menu_->setTitle(tr("Calculation"));
            fem_solution_menu_->menuAction()->setVisible(fem_active);
            fem_solution_menu_->setEnabled(fem_active&&!properties_dialog_);
            for(auto* action:fem_solution_menu_->actions())action->setEnabled(fem_active&&!properties_dialog_);
            fem_solution_menu_->findChild<QAction*>("femLinearStaticsAction")->setText(
                fem_analysis_kind()==document::FemAnalysisKind::ThermalMechanical?tr("Thermal-mechanical analysis"):
                fem_thermal_analysis()?tr("Steady-state thermal analysis"):tr("Linear statics"));
            auto* show=fem_solution_menu_->findChild<QAction*>("femShowDeformationAction");
            show->setText(fem_thermal_analysis()?tr("Show temperatures"):tr("Show deformation"));
            const bool available=fem_active&&active_fem_deformation();
            show->setEnabled(available&&!properties_dialog_);
            show->setChecked(available&&show_fem_deformation_);
        }
    }
    if(command_insert_menu_){command_insert_menu_->clear();command_insert_menu_->setEnabled(workspace_.size()!=0);}
    tools_toolbar_->clear();
    if(active_render_page_)return;
    if(active_sketch_id_.empty())sketch_edit_initial_.reset();
    else if(!sketch_edit_initial_||sketch_edit_initial_->id!=active_sketch_id_)
        if(const auto* sketch=active_sketch())sketch_edit_initial_=*sketch;
    if(cancel_sketch_action_)cancel_sketch_action_->setEnabled(!properties_dialog_&&sketch_edit_initial_&&sweep_profile_sketch_draft_);
    // QToolBar recalculates its minimum width after clear() and centers
    // narrower items. Give every command the width of the longest label.
    QTimer::singleShot(0,tools_toolbar_,[this] {
        int width=146;
        std::vector<QToolButton*> buttons;
        for(auto* action:tools_toolbar_->actions())
            if(auto* button=qobject_cast<QToolButton*>(tools_toolbar_->widgetForAction(action))) {
                width=std::max(width,button->sizeHint().width()); buttons.push_back(button);
            }
        for(auto* button:buttons) button->setFixedWidth(width);
    });
    const bool drawing =
        workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr;
    const QString heading_text = drawing ? tr("Výkres")
        : active_cam_body() ? tr("CAM")
        : active_render_body() ? tr("Render")
        : active_fem_body() ? tr("FEM")
        : !active_sketch_id_.empty() ? tr("Skica")
        : active_application_ == ApplicationMode::Modeling ? tr("Modelování")
        : active_application_ == ApplicationMode::Assembly ? tr("Sestava")
        : active_application_ == ApplicationMode::Mechanisms ? tr("Mechanisms")
        : active_application_ == ApplicationMode::SheetMetal ? application_actions_[static_cast<std::size_t>(ApplicationMode::SheetMetal)]->text()
        : active_application_ == ApplicationMode::Surface ? tr("Plochy")
        : active_application_ == ApplicationMode::Mech ? tr("MECH")
        : active_application_ == ApplicationMode::Piping ? tr("Potrubí")
        : tr("Výkres");
    const auto* toolbar_part = workspace_.open_part(workspace_.active_document_id());
    const bool toolbar_body = toolbar_part &&
        !toolbar_part->session.document().body_history.active_body_id().empty();
    const bool part_root = toolbar_part && !toolbar_body &&
        !drawing && active_sketch_id_.empty();
    if (!part_root) {
        auto* heading = new QLabel(heading_text, tools_toolbar_);
        heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        heading->setFont(tree_->font());
        heading->setStyleSheet(QStringLiteral("font-weight:600; padding:3px;"));
        tools_toolbar_->addWidget(heading);
    }
    const auto add_group_separator = [this] {
        tools_toolbar_->addSeparator();
        if(command_insert_menu_&&!command_insert_menu_->actions().empty())command_insert_menu_->addSeparator();
    };
    const auto add_command = [this](QAction* action,bool insert=true) {
        if (action == nullptr) return;
        tools_toolbar_->addAction(action);
        if(command_insert_menu_&&insert&&action!=selection_action_&&action->objectName()!="drawingSelectionAction")command_insert_menu_->addAction(action);
        if (auto* button=qobject_cast<QToolButton*>(tools_toolbar_->widgetForAction(action))) {
            new LeftAlignedCommandLabel(button);
            button->setObjectName("applicationCommandButton");
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QToolButton::customContextMenuRequested, this,
                [this, button, action](const QPoint& position) {
                    QMenu menu(this);
                    menu.setObjectName("commandGuideMenu");
                    const auto url = documentation::guide_url(application_settings_, action->objectName());
                    auto* guide = menu.addAction(application_settings_.text("menu.help.guide", "Show guide"));
                    guide->setObjectName("showCommandGuideAction");
                    guide->setEnabled(!url.isEmpty());
                    bool selected=false;
                    connect(guide, &QAction::triggered, &menu, [&selected] { selected=true; });
                    menu.exec(button->mapToGlobal(position));
                    // Opening a utility tab rebuilds this toolbar. Retire the
                    // context menu before its originating button is destroyed.
                    if(selected)QTimer::singleShot(0,this,[this,url]{open_browser(url);});
                });
            button->setProperty("zimaCommandAccent", action == sketch_action_);
            button->setProperty("zimaCommandActive",action->property("zimaCommandActive"));
            if(!action->property("zimaActiveFeedbackInstalled").toBool()) {
                action->setProperty("zimaActiveFeedbackInstalled",true);
                connect(action,&QAction::triggered,this,[this,action] {
                    auto* dialog=properties_dialog_;
                    if(!dialog || !dialog->isVisible())return;
                    const auto mark=[action=QPointer<QAction>(action)](bool active) {
                        if(!action)return;
                        action->setProperty("zimaCommandActive",active);
                        for(auto* object:action->associatedObjects())
                            if(auto* button=qobject_cast<QToolButton*>(object)) {
                                button->setProperty("zimaCommandActive",active);
                                button->style()->unpolish(button);button->style()->polish(button);button->update();
                            }
                    };
                    mark(true);
                    connect(dialog,&QDialog::finished,this,[mark]{mark(false);});
                });
            }
            if (action->menu() != nullptr) {
                button->setPopupMode(QToolButton::InstantPopup);
            }
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            button->setMinimumWidth(146);
        }
    };
    if (workspace_.size() == 0) return;
    if((active_fem_body()||active_render_body()||active_cam_body())&&active_sketch_id_.empty()) {
        // CAD actions may have shortcuts even when absent from the toolbar.
        // The analysis source is passive; its owner remains the material Body.
        for(auto* action:{sketch_action_,extrusion_action_,revolution_action_,construction_point_action_,construction_axis_action_,construction_plane_action_,
            curve_3d_action_,sweep2d_action_,sweep_3d_action_,helical_curve_action_,helical_sweep_action_,loft_action_,fillet_action_,chamfer_action_,shell_action_,
            thread_action_,holes_action_,shaft_thread_action_,drill_point_action_,mirror_action_,pattern_action_,body_scale_action_,symbol_action_,create_body_action_,create_fem_action_,create_render_action_,create_cam_action_,body_boolean_action_})if(action)action->setEnabled(false);
        for(const auto* name:{"featurePrototypeAction","surfaceFeatureAction","modelTwistAction","straightenAction","restoreShapeAction","generalSurfaceAction", "boundarySurfaceAction","surfaceSewingAction","surfaceIntersectionAction","sheetFromBodyAction","flatAction","bendAction","lineBendAction","sheetProfileAction"})
            if(auto* action=findChild<QAction*>(name))action->setEnabled(false);
        for(int i=0;i<6;++i)if(auto* action=findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i)))action->setEnabled(false);
        if(active_cam_body()) {
            if(!properties_dialog_)viewer_->set_candidate_filter([](const auto&){return false;});
            add_command(selection_action_);
            auto* setup=new QAction(resource_icon("properties"),tr("CAM setup properties"),tools_toolbar_);setup->setObjectName("camBodyPropertiesAction");
            setup->setEnabled(!properties_dialog_&&workspace_.active_document_id()==workspace_.displayed_document_id());
            connect(setup,&QAction::triggered,this,[this]{const auto* part=workspace_.open_part(workspace_.active_document_id());if(part)show_cam_body_properties(part->session.document().body_history.active_body_id());});add_command(setup);
            auto* calculate=new QAction(resource_icon("regenerate"),tr("Calculate CAM"),tools_toolbar_);calculate->setObjectName("camCalculateAction");calculate->setEnabled(setup->isEnabled());
            connect(calculate,&QAction::triggered,this,&AssemblyWorkspaceWindow::calculate_cam_body);add_command(calculate);
            auto* program=new QAction(resource_icon("properties"),tr("Generic NC program"),tools_toolbar_);program->setObjectName("camNcAction");program->setEnabled(setup->isEnabled());
            connect(program,&QAction::triggered,this,&AssemblyWorkspaceWindow::show_cam_nc);add_command(program);
            return;
        }
        if(active_render_body()) {
            if(!properties_dialog_)viewer_->set_candidate_filter([this](const auto& c){return active_render_object(c.owner_id)!=nullptr;});
            add_command(selection_action_);
            for(const auto kind:{document::RenderObjectKind::Camera,document::RenderObjectKind::PointLight,document::RenderObjectKind::Sun,document::RenderObjectKind::SpotLight}) {
                const auto key=kind==document::RenderObjectKind::Camera?"Camera":kind==document::RenderObjectKind::Sun?"Sun":kind==document::RenderObjectKind::SpotLight?"SpotLight":"PointLight";
                auto* action=new QAction(resource_icon(kind==document::RenderObjectKind::Camera?"view-perspective":"point"),
                    kind==document::RenderObjectKind::Camera?tr("Camera"):kind==document::RenderObjectKind::Sun?tr("Sun"):kind==document::RenderObjectKind::SpotLight?tr("Spotlight"):tr("Light"),tools_toolbar_);
                action->setObjectName(QString("render%1Action").arg(key));action->setEnabled(!properties_dialog_);
                connect(action,&QAction::triggered,this,[this,kind]{show_render_object_properties(kind);});add_command(action);
            }
            auto* settings=new QAction(resource_icon("properties"),tr("Render Body properties"),tools_toolbar_);settings->setObjectName("renderBodyPropertiesAction");
            settings->setEnabled(!properties_dialog_);connect(settings,&QAction::triggered,this,[this]{const auto* part=workspace_.open_part(workspace_.active_document_id());if(part)show_render_body_properties(part->session.document().body_history.active_body_id());});add_command(settings);
            add_group_separator();
            auto* render=new QAction(resource_icon("template-image"),tr("Render image"),tools_toolbar_);render->setObjectName("renderImageAction");render->setEnabled(!properties_dialog_);
            connect(render,&QAction::triggered,this,[this]{start_render();});add_command(render);
            auto* result=new QAction(resource_icon("template-image"),tr("Render gallery"),tools_toolbar_);result->setObjectName("renderShowResultAction");
            result->setEnabled(!properties_dialog_&&render_gallery_available());connect(result,&QAction::triggered,this,[this]{show_render_result();});add_command(result);
            return;
        }
        if(!properties_dialog_)viewer_->set_candidate_filter([this](const auto& c){return active_fem_condition(c.owner_id)!=nullptr;});
        add_command(selection_action_);
        auto* edit=new QAction(resource_icon("properties"),tr("FEM Body properties"),tools_toolbar_);edit->setObjectName("femBodyPropertiesAction");
        edit->setEnabled(!properties_dialog_);connect(edit,&QAction::triggered,this,[this]{
            const auto* part=workspace_.open_part(workspace_.active_document_id());if(part)show_fem_body_properties(part->session.document().body_history.active_body_id());});add_command(edit);
        for(auto kind:{document::FemConditionKind::Constraint,document::FemConditionKind::Force,document::FemConditionKind::Pressure,document::FemConditionKind::HeatSource,document::FemConditionKind::MeasurementPoint,document::FemConditionKind::MeasurementDistance}) {
            if(!document::fem_measurement(kind)&&!document::fem_condition_applies_to_analysis(kind,
                    fem_analysis_kind(),fem_expansion_analysis()))continue;
            const auto name=fem_condition_name(kind);
            const auto key=kind==document::FemConditionKind::MeasurementDistance?"MeasurementDistance":kind==document::FemConditionKind::MeasurementPoint?"MeasurementPoint":kind==document::FemConditionKind::HeatSource?"HeatSource":kind==document::FemConditionKind::Force?"Force":kind==document::FemConditionKind::Pressure?"Pressure":"Constraint";
            const auto icon=fem_condition_icon(kind);
            auto* action=new QAction(resource_icon(icon),name,tools_toolbar_);action->setObjectName(QString("fem%1Action").arg(key));
            action->setEnabled(!properties_dialog_);connect(action,&QAction::triggered,this,[this,kind]{show_fem_condition_properties(kind);});add_command(action);
        }
        add_group_separator();
        for(auto* action:fem_mesh_menu_->actions())add_command(action,false);
        if(fem_solution_menu_) {
            add_group_separator();
            for(auto* action:fem_solution_menu_->actions())add_command(action,false);
        }
        if(fem_results_menu_) {
            add_group_separator();
            for(auto* action:fem_results_menu_->actions()) {
                if(action->isSeparator())add_group_separator();
                else add_command(action,false);
            }
        }
        return;
    }
    if (drawing) {
        if (auto* drawing_toolbar =
                drawing_workspace_->findChild<QToolBar*>("drawingToolbar")) {
            for (auto* action : drawing_toolbar->actions()) {
                if (action->isSeparator()) tools_toolbar_->addSeparator();
                else {
                    if (action->objectName() == "drawingSelectionAction") {
                        action->setIcon(resource_icon("select"));
                    } else if (action->objectName() == "insertDrawingViewAction") {
                        action->setIcon(resource_icon("drawing"));
                    } else if (action->objectName() == "drawingDimensionAction") {
                        action->setIcon(resource_icon("drawing-dimension"));
                    }
                    const bool quick_export=action->objectName()=="drawingQuickExportPdfAction"||action->objectName()=="drawingQuickExportDxfAction";
                    add_command(action,!quick_export);
                }
            }
        }
        return;
    }

    const bool editing_nested_document =
        workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr &&
        workspace_.active_document_id() != workspace_.displayed_document_id();
    if (editing_nested_document && active_sketch_id_.empty()) {
        auto* return_action = new QAction(
            resource_icon("assembly"), tr("Zpět do sestavy"), tools_toolbar_);
        return_action->setObjectName("returnToAssemblyAction");
        return_action->setEnabled(!properties_dialog_&&active_sketch_id_.empty());
        connect(return_action, &QAction::triggered, this, [this] {
            deactivate_active_occurrence_for_test();
        });
        add_command(return_action,false);
        tools_toolbar_->addSeparator();
    }

    if (!active_sketch_id_.empty()) {
        const auto* template_part=workspace_.open_part(workspace_.active_document_id());
        if(!template_sketch()||(template_part&&template_part->native_drawing_template)) {
            tools_toolbar_->addSeparator();
            for(auto* action:{finish_sketch_action_,cancel_sketch_action_}) {
                add_command(action,false);
                if(auto* button=qobject_cast<QToolButton*>(tools_toolbar_->widgetForAction(action))) {
                    button->setMinimumHeight(40);button->setIconSize(QSize(26,26));
                }
            }
            add_group_separator();
        }
        if(symbol_document_sketch()) {
            family_table_action_->setEnabled(!properties_dialog_);
            add_command(family_table_action_,false);
        }
        if(const auto* sketch=template_sketch();sketch&&sketch->drawing_template->kind=="title_block") {
            if(!template_region_action_) {
                template_region_action_=new QAction(resource_icon("bom-region"),tr("Oblast kusovníku"),this);
                template_region_action_->setObjectName("templateRepeatRegionAction");
                connect(template_region_action_,&QAction::triggered,this,[this]{start_template_region();});
            }
            add_command(template_region_action_);add_group_separator();
        }
        if(!template_image_action_) {
            template_image_action_=new QAction(resource_icon("template-image"),tr("Obrázek"),this);
            template_image_action_->setObjectName("templateImageAction");
            connect(template_image_action_,&QAction::triggered,this,[this]{start_template_image();});
        }
        if(!tools_toolbar_->actions().back()->isSeparator())add_group_separator();
        add_command(sketch_normal_view_action_,false);
        add_command(sketch_flip_view_action_,false);
        add_command(sketch_rotate_view_action_,false);
        add_command(selection_action_);
        add_command(sketch_external_reference_action_);
        add_command(sketch_external_profile_action_);
        add_command(sketch_intersection_reference_action_);
        add_command(sketch_intersection_profile_action_);
        tools_toolbar_->addSeparator();
        add_command(sketch_trim_action_);
        add_command(sketch_curve_fillet_action_);
        add_command(sketch_mirror_action_);
        add_command(sketch_offset_action_);
        add_group_separator();
        for (auto* action : {sketch_point_action_, sketch_construction_action_,
                             sketch_segment_action_, sketch_common_tangent_action_,
                             sketch_polyline_action_,
                             sketch_rectangle_action_, sketch_slot_action_, sketch_polygon_action_,
                             sketch_circle_action_, sketch_arc_action_, sketch_three_point_arc_action_,
                             sketch_ellipse_action_, sketch_elliptical_arc_action_,
                             sketch_bspline_action_,
                             sketch_interpolating_spline_action_}) {
            add_command(action);
            if(action==sketch_polygon_action_)add_group_separator();
        }
        add_group_separator();
        add_command(sketch_constraints_action_);
        add_group_separator();
        add_command(sketch_universal_dimension_action_);
        add_group_separator();
        auto* dxf=findChild<QAction*>("sketchDxfAction");
        if(!dxf) {
            dxf=new QAction(resource_icon("export-dxf"),tr("DXF"),this);
            dxf->setObjectName("sketchDxfAction");
            connect(dxf,&QAction::triggered,this,[this]{import_sketch_dxf_block();});
        }
        dxf->setEnabled(!properties_dialog_);
        add_command(dxf);
        add_command(sketch_text_action_);
        add_command(symbol_action_);
        add_command(template_image_action_);
        add_group_separator();
        return;
    }

    const auto add_symbol_group = [&] {
        if (tools_toolbar_->actions().empty() || !tools_toolbar_->actions().back()->isSeparator())
            add_group_separator();
        add_command(symbol_action_);
    };
    if (active_application_ == ApplicationMode::Modeling) {
        const auto* modeling_part=workspace_.open_part(workspace_.active_document_id());
        const bool active_body=modeling_part&&!modeling_part->session.document().body_history.active_body_id().empty();
        mirror_action_->setEnabled(!properties_dialog_);
        pattern_action_->setEnabled(!properties_dialog_);
        body_scale_action_->setEnabled(modeling_part&&!properties_dialog_&&!active_body);
        if (const auto* part = workspace_.open_part(workspace_.active_document_id());
            part && workspace_.active_document_id() == workspace_.displayed_document_id()) {
            if (!create_body_action_) {
                create_body_action_ = new QAction(resource_icon("result-body"), tr("Vytvořit těleso"), this);
                create_body_action_->setObjectName("createBodyAction");
                connect(create_body_action_, &QAction::triggered, this, [this] { show_body_properties(); });
                body_boolean_action_ = new QAction(resource_icon("result-body"), tr("Boolean"), this);
                body_boolean_action_->setObjectName("bodyBooleanAction");
                connect(body_boolean_action_, &QAction::triggered, this, [this] { show_body_boolean_properties(); });
                create_fem_action_=new QAction(resource_icon("result-body"),tr("Create FEM"),this);
                create_fem_action_->setObjectName("createFemAction");
                connect(create_fem_action_,&QAction::triggered,this,[this]{show_fem_body_properties();});
                create_cam_action_=new QAction(resource_icon("result-body"),tr("Create CAM"),this);
                create_cam_action_->setObjectName("createCamAction");
                connect(create_cam_action_,&QAction::triggered,this,[this]{show_cam_body_properties();});
                create_render_action_=new QAction(resource_icon("render"),tr("Create Render"),this);
                create_render_action_->setObjectName("createRenderAction");
                connect(create_render_action_,&QAction::triggered,this,[this]{show_render_body_properties();});
            }
            const auto& graph = part->session.document().body_history;
            create_body_action_->setEnabled(!properties_dialog_ && graph.active_body_id().empty());
            create_fem_action_->setEnabled(!properties_dialog_ && graph.active_body_id().empty());
            create_cam_action_->setEnabled(!properties_dialog_ && graph.active_body_id().empty());
            create_render_action_->setEnabled(!properties_dialog_ && graph.active_body_id().empty());
            create_render_action_->setText(tr("Create Render"));
            auto* boolean = body_boolean_action_;
            boolean->setEnabled(!properties_dialog_ && graph.active_body_id().empty() &&
                graph.available_before(graph.insertion_cursor()).size() >= 2);
            boolean->setToolTip(tr("Součet, rozdíl nebo průnik dvou dostupných výsledků těles před místem vložení."));
            if (!active_body) {
                add_command(create_body_action_);
                add_command(create_fem_action_);
                add_command(section_action_);
                add_command(create_render_action_);
                add_command(create_cam_action_);
                add_command(mirror_action_);
                add_command(pattern_action_);
                add_command(body_scale_action_);
                add_command(boolean);
                return;
            }
        }
        if(!modeling_part){add_command(mirror_action_);add_command(pattern_action_);}
        add_command(selection_action_);
        if(auto* feature=findChild<QAction*>("featurePrototypeAction"))add_command(feature);
        add_group_separator();
        const auto* boundary_body=modeling_part?modeling_part->session.document().body_history.find(modeling_part->session.document().body_history.active_body_id()):nullptr;
        auto* twist=findChild<QAction*>("modelTwistAction");
        if(!twist) {
            twist=new QAction(resource_icon("sheet-twist"),tr("Krut"),this);
            twist->setObjectName("modelTwistAction");
            connect(twist,&QAction::triggered,this,[this]{show_primitive_properties(
                zima::document::FeatureKind::Feature,{},false,zima::document::FeatureType::Modeling,false,true);});
        }
        twist->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);
        QAction* projection=nullptr;
        if(active_body) {
            projection=findChild<QAction*>("projectionAction");
            if(!projection) {
                projection=new QAction(resource_icon("projection"),QString{},this);
                projection->setObjectName("projectionAction");
                connect(projection,&QAction::triggered,this,[this] {
                    if(properties_dialog_||!active_sketch_id_.empty()||active_application_!=ApplicationMode::Modeling)return;
                    show_primitive_properties(document::FeatureKind::Feature,{},false,document::FeatureType::Projection);
                });
            }
            projection->setText(tr("Project sketch"));
            projection->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);
        }
        for(int i=0;i<6;++i) {
            if(auto* shortcut=findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i)))add_command(shortcut);
        }
        add_command(twist);
        if(projection)add_command(projection);
        auto* projection_relief=findChild<QAction*>("projectionReliefAction");
        if(!projection_relief) {
            projection_relief=new QAction(resource_icon("projection"),QString{},this);
            projection_relief->setObjectName("projectionReliefAction");
            connect(projection_relief,&QAction::triggered,this,[this] {
                show_primitive_properties(document::FeatureKind::Feature,{},false,document::FeatureType::Projection,false,false,false,true);
            });
        }
        projection_relief->setText(tr("Project relief"));
        projection_relief->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);
        add_command(projection_relief);
        for(bool relief:{false,true}) {
            const auto name=relief?"wrapReliefAction":"wrapSketchAction";
            auto* action=findChild<QAction*>(name);
            if(!action) {
                action=new QAction(resource_icon("wrap"),QString{},this);action->setObjectName(name);
                connect(action,&QAction::triggered,this,[this,relief] {
                    show_primitive_properties(document::FeatureKind::Feature,{},false,document::FeatureType::Wrapping,false,false,relief);
                });
            }
            action->setText(relief?tr("Wrap relief"):tr("Wrap sketch"));
            action->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);
            add_command(action);
        }
        auto* surface=findChild<QAction*>("surfaceFeatureAction");
        if(!surface) {
            surface=new QAction(resource_icon("boundary-surface"),QString{},this);
            surface->setObjectName("surfaceFeatureAction");
            connect(surface,&QAction::triggered,this,[this] {
                show_primitive_properties(document::FeatureKind::Feature,{},false,
                    document::FeatureType::Modeling,false,false,false,false,true);
            });
        }
        surface->setText(tr("Plocha"));
        surface->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);
        add_command(surface);
        add_group_separator();
        add_command(sweep2d_action_);
        add_command(curve_3d_action_);
        add_command(sweep_3d_action_);
        add_command(helical_curve_action_);
        add_command(helical_sweep_action_);
        add_command(loft_action_);
        for(bool restore:{false,true}) {
            const auto name=restore?"restoreShapeAction":"straightenAction";
            auto* action=findChild<QAction*>(name);
            if(!action) {
                action=new QAction(resource_icon(restore?"restore-shape":"straighten"),restore?tr("Restore shape"):tr("Straighten"),this);
                action->setObjectName(name);connect(action,&QAction::triggered,this,[this,restore]{show_solid_state_properties(restore);});
            }
            action->setEnabled(modeling_part&&boundary_body&&!boundary_body->derived_copy&&!properties_dialog_&&
                !workspace::solid_state_sources(modeling_part->session.document(),restore).empty());
            add_command(action);
        }
        add_group_separator();
        add_command(fillet_action_);
        add_command(chamfer_action_);
        add_command(shell_action_);
        add_group_separator();
        add_command(thread_action_);
        add_command(holes_action_);
        add_command(shaft_thread_action_);
        add_command(drill_point_action_);
        if (active_body) {
            add_group_separator();
            add_command(mirror_action_);
            add_command(pattern_action_);
        }
        add_symbol_group();
        return;
    }
    if (active_application_ == ApplicationMode::Mech) {
        const auto* part=workspace_.open_part(workspace_.active_document_id());
        const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
        add_command(selection_action_);add_group_separator();
        auto* keyway=findChild<QAction*>("mechKeywayAction");
        if(!keyway){keyway=new QAction(resource_icon("keyway"),tr("Drážka pro pero"),this);
            keyway->setObjectName("mechKeywayAction");connect(keyway,&QAction::triggered,this,[this]{show_primitive_properties(document::FeatureKind::MechKeyway);});}
        keyway->setEnabled(body&&!body->derived_copy&&!body->suppressed&&!properties_dialog_);add_command(keyway);
        auto* groove=findChild<QAction*>("mechGrooveAction");
        if(!groove){groove=new QAction(resource_icon("groove"),tr("Groove"),this);
            groove->setObjectName("mechGrooveAction");connect(groove,&QAction::triggered,this,[this]{show_primitive_properties(document::FeatureKind::MechGroove);});}
        groove->setEnabled(body&&!body->derived_copy&&!body->suppressed&&!properties_dialog_);add_command(groove);
        add_group_separator();add_command(mirror_action_);add_command(pattern_action_);add_symbol_group();return;
    }
    if (active_application_ == ApplicationMode::Surface) {
        const auto* modeling_part=workspace_.open_part(
            workspace_.active_document_id());
        const auto* boundary_body=modeling_part?
            modeling_part->session.document().body_history.find(
                modeling_part->session.document().body_history.active_body_id()):nullptr;
        add_command(selection_action_);
        add_group_separator();
        auto* general=findChild<QAction*>("generalSurfaceAction");
        if(!general){general=new QAction(resource_icon("boundary-surface"),tr("Obecná plocha"),this);
            general->setObjectName("generalSurfaceAction");connect(general,&QAction::triggered,this,[this]{show_sweep_properties(zima::document::FeatureKind::GeneralSurface,{});});}
        general->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(general);
        auto* boundary=findChild<QAction*>("boundarySurfaceAction");
        if(!boundary){boundary=new QAction(resource_icon("boundary-surface"),tr("Zaplnit plochu"),this);
            boundary->setObjectName("boundarySurfaceAction");connect(boundary,&QAction::triggered,this,[this]{show_boundary_surface_properties();});}
        boundary->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(boundary);
        auto* sewing=findChild<QAction*>("surfaceSewingAction");
        if(!sewing){sewing=new QAction(resource_icon("surface-sewing"),tr("Sešít plochy"),this);
            sewing->setObjectName("surfaceSewingAction");connect(sewing,&QAction::triggered,this,[this]{show_surface_sewing_properties();});}
        sewing->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(sewing);
        auto* intersection=findChild<QAction*>("surfaceIntersectionAction");
        if(!intersection){intersection=new QAction(resource_icon("sketch-3d"),tr("Průsečík ploch"),this);
            intersection->setObjectName("surfaceIntersectionAction");connect(intersection,&QAction::triggered,this,[this]{show_surface_intersection_properties();});}
        intersection->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(intersection);
        auto* trim=findChild<QAction*>("surfaceTrimAction");
        if(!trim){trim=new QAction(resource_icon("boundary-surface"),tr("Oříznout plochu"),this);
            trim->setObjectName("surfaceTrimAction");connect(trim,&QAction::triggered,this,[this]{show_surface_trim_properties();});}
        trim->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(trim);
        auto* skin=findChild<QAction*>("surfaceFromSolidAction");
        if(!skin){skin=new QAction(resource_icon("surface-from-solid"),tr("Plochy z tělesa"),this);
            skin->setObjectName("surfaceFromSolidAction");connect(skin,&QAction::triggered,this,[this]{start_shell(true);});}
        const auto* skin_input=modeling_part?zima::workspace::calculated_operation_input(modeling_part->session,{}):nullptr;
        bool skin_source=skin_input&&skin_input->has_solid();
        if(modeling_part&&!skin_source)for(const auto& body:modeling_part->session.document().body_history.bodies())try {
            if(zima::workspace::shell_source_input(*modeling_part,{},body.scope.id)){skin_source=true;break;}
        }catch(const std::exception&){}
        skin->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_&&skin_source);
        add_command(skin);
        auto* thicken=findChild<QAction*>("surfaceThickenAction");
        if(!thicken){thicken=new QAction(resource_icon("surface-thicken"),tr("Zesílit plochu"),this);
            thicken->setObjectName("surfaceThickenAction");connect(thicken,&QAction::triggered,this,[this]{show_surface_thicken_properties();});}
        thicken->setEnabled(boundary_body&&!boundary_body->derived_copy&&!properties_dialog_);add_command(thicken);
        add_group_separator();
        add_command(fillet_action_);
        add_command(chamfer_action_);
        if (boundary_body) {
            add_group_separator();
            add_command(mirror_action_);
            add_command(pattern_action_);
        }
        add_symbol_group();
        return;
    }
    if (active_application_ == ApplicationMode::Mechanisms) {
        add_command(selection_action_);
        auto* placeholder=findChild<QAction*>("mechanismsPlaceholderAction");
        if(!placeholder) {
            placeholder=new QAction(this);
            placeholder->setObjectName("mechanismsPlaceholderAction");
        }
        placeholder->setText(tr("In preparation"));
        placeholder->setToolTip(tr("No mechanisms yet. Motion tools are being prepared."));
        placeholder->setEnabled(false);
        add_command(placeholder,false);
        return;
    }
    if (active_application_ == ApplicationMode::Assembly) {
        add_command(selection_action_);
        add_command(section_action_);
        auto* create=findChild<QAction*>("createComponentAction");
        if(!create) {
            create=new QAction(resource_icon("new"),tr("Vytvořit komponentu"),this);
            create->setObjectName("createComponentAction");
            connect(create,&QAction::triggered,this,&AssemblyWorkspaceWindow::create_component);
        }
        create->setEnabled(!properties_dialog_ && active_sketch_id_.empty());
        add_command(create);
        add_command(insert_action_);
        add_group_separator();
        if(auto* feature=findChild<QAction*>("featurePrototypeAction"))add_command(feature);
        for(int i=0;i<4;++i)
            if(auto* shortcut=findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i)))add_command(shortcut);
        add_command(curve_3d_action_);
        for(bool wrap:{false,true}) {
            const auto name=wrap?"assemblyWrapCurveAction":"assemblyProjectCurveAction";
            auto* action=findChild<QAction*>(name);
            if(!action) {
                action=new QAction(resource_icon(wrap?"wrap":"projection"),QString{},this);action->setObjectName(name);
                connect(action,&QAction::triggered,this,[this,wrap] {
                    show_primitive_properties(document::FeatureKind::Feature,{},false,
                        wrap?document::FeatureType::Wrapping:document::FeatureType::Projection);
                });
            }
            action->setText(tr(wrap?"Wrap curve":"Project curve"));action->setEnabled(!properties_dialog_);add_command(action);
        }
        tools_toolbar_->addSeparator();
        mirror_action_->setEnabled(!properties_dialog_);add_command(mirror_action_);
        pattern_action_->setEnabled(!properties_dialog_);add_command(pattern_action_);
        add_symbol_group();
        return;
    }
    if(active_application_==ApplicationMode::SheetMetal) {
        add_command(selection_action_);
        if(workspace_.open_part(workspace_.active_document_id())) {
            auto* properties=findChild<QAction*>("sheetMetalPropertiesAction");
            if(!properties) {
                properties=new QAction(resource_icon("settings"),tr("Vlastnosti plechu…"),this);
                properties->setObjectName("sheetMetalPropertiesAction");
                connect(properties,&QAction::triggered,this,[this]{edit_file_settings(true);});
            }
            properties->setEnabled(!properties_dialog_ || properties_dialog_->objectName()=="fileSettingsDialog");
            add_command(properties,false);add_group_separator();
            auto* convert=findChild<QAction*>("sheetFromBodyAction");
            if(!convert){convert=new QAction(resource_icon("sheet-from-body"),tr("Plech z tělesa"),this);convert->setObjectName("sheetFromBodyAction");
                connect(convert,&QAction::triggered,this,[this]{show_sheet_from_body();});}
            const auto& part=workspace_.open_part(workspace_.active_document_id())->session.document();
            const auto* body=part.body_history.find(part.body_history.active_body_id());
            convert->setEnabled(!properties_dialog_&&body&&!body->derived_copy);add_command(convert);
            auto* flat=findChild<QAction*>("flatAction");
            if(!flat) {
                flat=new QAction(resource_icon("flat"),tr("Tabule"),this);flat->setObjectName("flatAction");
                connect(flat,&QAction::triggered,this,[this]{show_sketch_properties({},false,false,true);});
            }
            flat->setEnabled(!properties_dialog_);add_command(flat);
            auto* bend=findChild<QAction*>("bendAction");
            if(!bend) {
                bend=new QAction(resource_icon("bend"),tr("Ohyb"),this);bend->setObjectName("bendAction");
                connect(bend,&QAction::triggered,this,[this]{show_sketch_properties({},false,true);});
            }
            bend->setEnabled(!properties_dialog_);add_command(bend);
            auto* line_bend=findChild<QAction*>("lineBendAction");
            if(!line_bend) {
                line_bend=new QAction(resource_icon("line-bend"),
                    tr("Ohyb podle čáry"),this);
                line_bend->setObjectName("lineBendAction");
                connect(line_bend,&QAction::triggered,this,[this] {
                    show_sketch_properties({},false,false,false,false,true);
                });
            }
            line_bend->setEnabled(!properties_dialog_);add_command(line_bend);
            auto* profile=findChild<QAction*>("sheetProfileAction");
            if(!profile) {
                profile=new QAction(resource_icon("sheet-profile"),tr("Profil"),this);
                profile->setObjectName("sheetProfileAction");
                connect(profile,&QAction::triggered,this,[this] {
                    show_sketch_properties({},false,false,false,true);
                });
            }
            profile->setEnabled(!properties_dialog_);add_command(profile);
            auto* rotation=findChild<QAction*>("sheetRevolutionAction");
            if(!rotation) {
                rotation=new QAction(resource_icon("sheet-revolve"),tr("Rotační plech"),this);rotation->setObjectName("sheetRevolutionAction");
                connect(rotation,&QAction::triggered,this,[this]{show_primitive_properties(zima::document::FeatureKind::Revolution,{},true);});
            }
            rotation->setEnabled(!properties_dialog_);add_command(rotation);
            auto* twist=findChild<QAction*>("twistedSheetAction");
            if(!twist) {
                twist=new QAction(resource_icon("sheet-twist"),tr("Kroucený plech"),this);
                twist->setObjectName("twistedSheetAction");
                connect(twist,&QAction::triggered,this,[this]{
                    show_primitive_properties(zima::document::FeatureKind::TwistedSheet,{},true);});
            }
            twist->setEnabled(!properties_dialog_);add_command(twist);
            auto* form=findChild<QAction*>("sheetFormAction");
            if(!form){form=new QAction(resource_icon("sheet-form"),tr("Form"),this);
                form->setObjectName("sheetFormAction");
                connect(form,&QAction::triggered,this,[this]{show_primitive_properties(zima::document::FeatureKind::SheetForm,{},true);});}
            form->setEnabled(!properties_dialog_&&body&&!body->derived_copy&&!body->suppressed);add_command(form);
            auto* transition=findChild<QAction*>("sheetTransitionAction");
            if(!transition){transition=new QAction(resource_icon("sheet-transition"),tr("Přechod plechu"),this);transition->setObjectName("sheetTransitionAction");connect(transition,&QAction::triggered,this,[this]{show_sheet_transition_properties();});}
            const bool transition_available=!properties_dialog_ && !zima::document::has_sheet_transition(
                workspace_.open_part(workspace_.active_document_id())->session.document());
            transition->setEnabled(transition_available);add_command(transition);
            auto* rectangular=findChild<QAction*>("rectangularSheetTransitionAction");
            if(!rectangular){rectangular=new QAction(resource_icon("sheet-transition-rectangular"),tr("Obdélníkový přechod plechu"),this);rectangular->setObjectName("rectangularSheetTransitionAction");connect(rectangular,&QAction::triggered,this,[this]{show_sheet_transition_properties({},true);});}
            rectangular->setEnabled(transition_available);add_command(rectangular);add_group_separator();
            auto* cut=findChild<QAction*>("sheetCutAction");
            if(!cut) {
                cut=new QAction(resource_icon("sheet-cut"),tr("Řez plechem"),this);cut->setObjectName("sheetCutAction");
                connect(cut,&QAction::triggered,this,[this]{show_primitive_properties(zima::document::FeatureKind::Extrusion,{},true);});
            }
            cut->setEnabled(!properties_dialog_);add_command(cut);add_group_separator();
            for(const bool unfold:{true,false}) {
                const char* object=unfold?"unbendAction":"bendBackAction";
                auto* action=findChild<QAction*>(object);
                if(!action) {
                    action=new QAction(resource_icon(unfold?"unbend":"bend-back"),unfold?tr("Rozvinout"):tr("Ohnout zpět"),this);
                    action->setObjectName(object);connect(action,&QAction::triggered,this,[this,unfold]{show_sheet_state_properties(unfold);});
                }
                action->setEnabled(!properties_dialog_);add_command(action);
                if(!unfold)add_group_separator();
            }
            auto* dxf=findChild<QAction*>("sheetDxfAction");
            if(!dxf){dxf=new QAction(resource_icon("export-dxf"),tr("DXF"),this);dxf->setObjectName("sheetDxfAction");
                connect(dxf,&QAction::triggered,this,[this]{export_sheet_dxf();});}
            dxf->setEnabled(!properties_dialog_);add_command(dxf);
            add_group_separator();
            mirror_action_->setEnabled(!properties_dialog_);add_command(mirror_action_);
            pattern_action_->setEnabled(!properties_dialog_);add_command(pattern_action_);
            add_symbol_group();
            return;
        }
    }
    auto* placeholder = new QAction(
        active_application_ == ApplicationMode::SheetMetal
            ? tr("Příkazy plechu – připravuje se")
        : active_application_ == ApplicationMode::Surface
            ? tr("Příkazy ploch – připravuje se")
            : tr("Příkazy potrubí – připravuje se"), tools_toolbar_);
    placeholder->setEnabled(false);
    add_command(placeholder);
    add_symbol_group();
}

void AssemblyWorkspaceWindow::sync_sketch_tool_action_checks() {
    const auto set_checked = [](QAction* action, bool checked) {
        if (action == nullptr) return;
        const QSignalBlocker blocker(action);
        action->setChecked(checked);
    };
    set_checked(sketch_point_action_, sketch_point_active_);
    set_checked(sketch_construction_action_,
        sketch_segment_active_ && sketch_segment_construction_);
    set_checked(sketch_segment_action_, sketch_segment_active_ &&
        !sketch_segment_construction_ && !sketch_polyline_active_);
    set_checked(sketch_polyline_action_,
        sketch_segment_active_ && sketch_polyline_active_);
    set_checked(sketch_rectangle_action_, sketch_rectangle_active_);
    set_checked(sketch_slot_action_, sketch_slot_active_);
    set_checked(sketch_polygon_action_, sketch_polygon_active_);
    set_checked(sketch_circle_action_, sketch_circle_active_);
    set_checked(sketch_arc_action_, sketch_arc_active_&&!sketch_three_point_arc_);
    set_checked(sketch_three_point_arc_action_, sketch_arc_active_&&sketch_three_point_arc_);
    set_checked(sketch_ellipse_action_, sketch_ellipse_active_);
    set_checked(sketch_elliptical_arc_action_,
        sketch_elliptical_arc_active_);
    set_checked(sketch_bspline_action_,
        sketch_bspline_active_ && !sketch_bspline_interpolating_);
    set_checked(sketch_interpolating_spline_action_,
        sketch_bspline_active_ && sketch_bspline_interpolating_);
    set_checked(sketch_trim_action_, sketch_trim_active_);
    set_checked(sketch_curve_fillet_action_, sketch_corner_fillet_active_ && sketch_curve_fillet_materialized_);
    set_checked(sketch_universal_dimension_action_,
        sketch_universal_dimension_active_);
    set_checked(selection_action_, !sketch_command_active());
}
bool AssemblyWorkspaceWindow::sketch_command_active() const {
    return sketch_point_active_ || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_circle_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
        sketch_elliptical_arc_active_ || sketch_bspline_active_ ||
        sketch_external_reference_active_ || sketch_trim_active_ ||
        sketch_corner_fillet_active_ || sketch_offset_dialog_ || sketch_mirror_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_universal_dimension_active_ ||
        sketch_line_pair_dimension_active_ || pending_sketch_dimension_.has_value();
}

} // namespace zima::app
