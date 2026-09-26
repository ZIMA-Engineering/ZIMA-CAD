#include "workspace_internal.hpp"
#include "../command_button_paint.hpp"

namespace zima::app {
using namespace workspace_detail;

namespace {


} // namespace


void AssemblyWorkspaceWindow::update_document_area_visibility() {
    const bool has_document = workspace_.size() != 0;
    if (tabs_ != nullptr) tabs_->setVisible(has_document);
    if (document_splitter_ != nullptr) document_splitter_->setVisible(has_document);
    save_action_->setEnabled(has_document);
    close_document_action_->setEnabled(has_document);
    regenerate_document_action_->setEnabled(has_document);
    fit_view_action_->setEnabled(has_document);
    selection_action_->setEnabled(has_document);
    selection_filter_combo_->setEnabled(has_document &&
        !workspace_.open_drawing(workspace_.active_document_id()));
    export_action_->setEnabled(has_document);
    parameters_action_->setEnabled(has_document);
    const bool has_editable_model = has_document &&
        (workspace_.open_part(workspace_.active_document_id()) != nullptr ||
         workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    material_action_->setEnabled(workspace_.open_part(workspace_.active_document_id()) != nullptr);
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
    refresh_delete_file_actions();
    if (!has_document && state_ != nullptr) state_->setText(tr("Připraveno."));
}

void AssemblyWorkspaceWindow::update_application_actions() {
    if(section_action_)section_action_->setEnabled(workspace_.active_document_id()==workspace_.displayed_document_id()&&!workspace_.open_drawing(workspace_.displayed_document_id())&&!properties_dialog_&&active_sketch_id_.empty()&&!template_sketch());
    for (auto* action : application_actions_) action->setEnabled(false);
    if (workspace_.size() == 0) return;
    const auto owner=workspace_.active_document_id();
    const auto saved=document_application_modes_.find(owner);
    active_application_=saved==document_application_modes_.end()
        ? (workspace_.open_assembly(owner)?ApplicationMode::Assembly:ApplicationMode::Modeling) : saved->second;
    if (workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr) {
        application_actions_[static_cast<std::size_t>(ApplicationMode::Drawing)]
            ->setEnabled(true);
        active_application_ = ApplicationMode::Drawing;
    } else if (workspace_.open_part(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::SheetMetal}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Modeling;
        }
    } else if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::Assembly,
                                ApplicationMode::SheetMetal, ApplicationMode::Surface}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Assembly;
        }
    }
    application_actions_[static_cast<std::size_t>(active_application_)]->setChecked(true);
    if(properties_dialog_ || !active_sketch_id_.empty() || section_dialog_)
        for(auto* action:application_actions_)action->setEnabled(false);
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
}

void AssemblyWorkspaceWindow::rebuild_application_toolbar() {
    if (tools_toolbar_ == nullptr) return;
    if(command_insert_menu_){command_insert_menu_->clear();command_insert_menu_->setEnabled(workspace_.size()!=0);}
    tools_toolbar_->clear();
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
        : !active_sketch_id_.empty() ? tr("Skica")
        : active_application_ == ApplicationMode::Modeling ? tr("Modelování")
        : active_application_ == ApplicationMode::Assembly ? tr("Sestava")
        : active_application_ == ApplicationMode::SheetMetal ? application_actions_[static_cast<std::size_t>(ApplicationMode::SheetMetal)]->text()
        : active_application_ == ApplicationMode::Surface ? tr("Plochy")
        : active_application_ == ApplicationMode::Piping ? tr("Potrubí")
        : tr("Výkres");
    if(!drawing && active_sketch_id_.empty() && workspace_.open_part(workspace_.active_document_id())) {
        auto* selector=new QComboBox(tools_toolbar_);selector->setObjectName("applicationModeSelector");
        for(const auto mode:{ApplicationMode::Modeling,ApplicationMode::SheetMetal}) {
            const auto index=static_cast<std::size_t>(mode);
            selector->addItem(application_actions_[index]->text(),static_cast<int>(mode));
        }
        selector->setCurrentIndex(selector->findData(static_cast<int>(active_application_)));
        selector->setEnabled(application_actions_[static_cast<std::size_t>(active_application_)]->isEnabled());
        selector->setFont(tree_->font());selector->setMinimumWidth(146);
        connect(selector,&QComboBox::activated,this,[this,selector](int index) {
            const auto mode=selector->itemData(index).toInt();
            // The command rebuilds this toolbar; retire the popup before dispatch.
            QTimer::singleShot(0,this,[this,mode]{application_actions_[mode]->trigger();});
        });
        tools_toolbar_->addWidget(selector);
    } else {
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
    if (editing_nested_document) {
        auto* return_action = new QAction(
            resource_icon("assembly"), tr("Zpět do sestavy"), tools_toolbar_);
        return_action->setEnabled(!properties_dialog_&&active_sketch_id_.empty());
        connect(return_action, &QAction::triggered, this, [this] {
            deactivate_active_occurrence_for_test();
        });
        add_command(return_action,false);
        tools_toolbar_->addSeparator();
    }

    if (!active_sketch_id_.empty()) {
        if(symbol_document_sketch()) {
            auto* choice=new QComboBox(tools_toolbar_);choice->setObjectName("symbolSketchChoice");
            choice->setToolTip(tr("Skica"));
            const auto* part=workspace_.open_part(workspace_.active_document_id());
            for(const auto& s:part->session.document().sketches)
                choice->addItem(QString::fromStdString(s.name),QString::fromStdString(s.id));
            choice->setCurrentIndex(choice->findData(QString::fromStdString(active_sketch_id_)));
            choice->setEnabled(!properties_dialog_);
            tools_toolbar_->addWidget(choice);
            connect(choice,&QComboBox::activated,this,[this,choice](int index){
                const auto id=choice->itemData(index).toString().toStdString();
                cancel_sketch_segment();clear_selected_sketch_geometry();active_sketch_id_=id;
                QTimer::singleShot(0,this,[this]{refresh_scene();});
            });
        }
        if(const auto* sketch=template_sketch();sketch&&sketch->drawing_template->kind=="title_block") {
            if(!template_region_action_) {
                template_region_action_=new QAction(resource_icon("bom-region"),tr("Oblast kusovníku"),this);
                template_region_action_->setObjectName("templateRepeatRegionAction");
                connect(template_region_action_,&QAction::triggered,this,[this]{start_template_region();});
            }
            if(!template_image_action_) {
                template_image_action_=new QAction(resource_icon("template-image"),tr("Obrázek"),this);
                template_image_action_->setObjectName("templateImageAction");
                connect(template_image_action_,&QAction::triggered,this,[this]{start_template_image();});
            }
            add_command(template_region_action_);add_command(template_image_action_);add_group_separator();
        }
        add_command(sketch_normal_view_action_,false);
        add_command(sketch_flip_view_action_,false);
        add_command(sketch_rotate_view_action_,false);
        add_command(selection_action_);
        add_command(sketch_external_reference_action_);
        add_command(sketch_external_profile_action_);
        tools_toolbar_->addSeparator();
        add_command(sketch_trim_action_);
        add_command(sketch_mirror_action_);
        add_command(sketch_offset_action_);
        add_group_separator();
        for (auto* action : {sketch_point_action_, sketch_construction_action_,
                             sketch_segment_action_, sketch_common_tangent_action_,
                             sketch_polyline_action_,
                             sketch_rectangle_action_, sketch_polygon_action_,
                             sketch_circle_action_, sketch_arc_action_,
                             sketch_ellipse_action_, sketch_elliptical_arc_action_,
                             sketch_bspline_action_,
                             sketch_interpolating_spline_action_}) {
            add_command(action);
        }
        add_group_separator();
        add_command(sketch_constraints_action_);
        add_group_separator();
        add_command(sketch_universal_dimension_action_);
        add_command(sketch_text_action_);
        add_command(symbol_action_);
        add_group_separator();
        if(!template_sketch()&&!symbol_document_sketch())add_command(finish_sketch_action_,false);
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
        if(!active_body){add_command(mirror_action_);add_command(pattern_action_);}
        if (const auto* part = workspace_.open_part(workspace_.active_document_id());
            part && workspace_.active_document_id() == workspace_.displayed_document_id()) {
            if (!create_body_action_) {
                create_body_action_ = new QAction(resource_icon("result-body"), tr("Vytvořit těleso"), this);
                create_body_action_->setObjectName("createBodyAction");
                connect(create_body_action_, &QAction::triggered, this, [this] { show_body_properties(); });
                body_boolean_action_ = new QAction(resource_icon("result-body"), tr("Boolean"), this);
                body_boolean_action_->setObjectName("bodyBooleanAction");
                connect(body_boolean_action_, &QAction::triggered, this, [this] { show_body_boolean_properties(); });
            }
            const auto& graph = part->session.document().body_history;
            create_body_action_->setEnabled(!properties_dialog_ && graph.active_body_id().empty());
            if (graph.active_body_id().empty()) add_command(create_body_action_);
            auto* boolean = body_boolean_action_;
            boolean->setEnabled(!properties_dialog_ && graph.active_body_id().empty() &&
                graph.available_before(graph.insertion_cursor()).size() >= 2);
            boolean->setToolTip(tr("Součet, rozdíl nebo průnik dvou dostupných výsledků těles před místem vložení."));
            if (graph.active_body_id().empty()) add_command(boolean);
            if (graph.active_body_id().empty()) tools_toolbar_->addSeparator();
            if (graph.active_body_id().empty() && (!graph.bodies().empty() ||
                    part->session.document().history_order.empty())) { add_symbol_group(); return; }
        }
        add_command(selection_action_);
        if(auto* feature=findChild<QAction*>("featurePrototypeAction"))add_command(feature);
        add_group_separator();
        for(int i=0;i<6;++i)
            if(auto* shortcut=findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i)))add_command(shortcut);
        add_group_separator();
        add_command(curve_3d_action_);
        add_command(sweep2d_action_);
        add_command(sweep_3d_action_);
        add_command(helical_sweep_action_);
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
    if (active_application_ == ApplicationMode::Assembly) {
        add_command(selection_action_);
        add_command(insert_action_);
        add_group_separator();
        for (auto* action : {construction_point_action_, construction_axis_action_,
                             construction_plane_action_}) {
            add_command(action);
        }
        add_group_separator();
        add_command(sketch_action_);
        add_command(curve_3d_action_);
        tools_toolbar_->addSeparator();
        add_command(extrusion_action_);
        add_command(revolution_action_);
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
            convert->setEnabled(!properties_dialog_&&body&&body->entries.empty()&&!body->derived_copy);add_command(convert);
            auto* flat=findChild<QAction*>("flatAction");
            if(!flat) {
                flat=new QAction(resource_icon("flat"),tr("Tabule"),this);flat->setObjectName("flatAction");
                connect(flat,&QAction::triggered,this,[this]{show_sketch_properties({},false,false,true);});
            }
            flat->setEnabled(!properties_dialog_);add_command(flat);
            auto* bend=findChild<QAction*>("bendAction");
            if(!bend) {
                bend=new QAction(resource_icon("bend"),tr("Profil plechu"),this);bend->setObjectName("bendAction");
                connect(bend,&QAction::triggered,this,[this]{show_sketch_properties({},false,true);});
            }
            bend->setEnabled(!properties_dialog_);add_command(bend);
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
            auto* transition=findChild<QAction*>("sheetTransitionAction");
            if(!transition){transition=new QAction(resource_icon("sheet-transition"),tr("Přechod plechu"),this);transition->setObjectName("sheetTransitionAction");connect(transition,&QAction::triggered,this,[this]{show_sheet_transition_properties();});}
            transition->setEnabled(!properties_dialog_);add_command(transition);add_group_separator();
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
    set_checked(sketch_polygon_action_, sketch_polygon_active_);
    set_checked(sketch_circle_action_, sketch_circle_active_);
    set_checked(sketch_arc_action_, sketch_arc_active_);
    set_checked(sketch_ellipse_action_, sketch_ellipse_active_);
    set_checked(sketch_elliptical_arc_action_,
        sketch_elliptical_arc_active_);
    set_checked(sketch_bspline_action_,
        sketch_bspline_active_ && !sketch_bspline_interpolating_);
    set_checked(sketch_interpolating_spline_action_,
        sketch_bspline_active_ && sketch_bspline_interpolating_);
    set_checked(sketch_trim_action_, sketch_trim_active_);
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
