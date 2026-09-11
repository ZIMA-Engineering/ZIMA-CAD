#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;

namespace {


// Stylesheets may bypass CE_ToolButtonLabel in a proxy style. Paint the
// command label explicitly while retaining the real toolbar QAction.
class LeftAlignedCommandLabel final : public QObject {
public:
    explicit LeftAlignedCommandLabel(QToolButton* button) : QObject(button), button_(button) {
        button->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type()!=QEvent::Paint) return false;
        QStyleOptionToolButton option;
        option.initFrom(button_);
        option.iconSize=button_->iconSize();
        option.toolButtonStyle=Qt::ToolButtonTextBesideIcon;
        if (button_->isDown()) option.state|=QStyle::State_Sunken;
        if (button_->isChecked()) option.state|=QStyle::State_On;
        if (button_->autoRaise()) option.state|=QStyle::State_AutoRaise;
        if (button_->menu()) option.features|=QStyleOptionToolButton::HasMenu;
        QPainter painter(button_);
        button_->style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,button_);
        const int extent=button_->iconSize().width();
        const QRect icon_rect(6,(button_->height()-extent)/2,extent,extent);
        const auto icon=button_->icon();
        if (!icon.isNull()) icon.paint(&painter,icon_rect,Qt::AlignCenter,
            button_->isEnabled()?QIcon::Normal:QIcon::Disabled,
            button_->isChecked()?QIcon::On:QIcon::Off);
        const int left=icon.isNull()?6:icon_rect.right()+5;
        button_->style()->drawItemText(&painter,button_->rect().adjusted(left,0,-16,0),
            Qt::AlignLeft|Qt::AlignVCenter|Qt::TextShowMnemonic,
            button_->palette(),button_->isEnabled(),button_->text(),QPalette::ButtonText);
        return true;
    }
private:
    QToolButton* button_;
};

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
    selection_filter_combo_->setEnabled(has_document);
    export_action_->setEnabled(has_document);
    parameters_action_->setEnabled(has_document);
    const bool has_editable_model = has_document &&
        (workspace_.open_part(workspace_.active_document_id()) != nullptr ||
         workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    material_action_->setEnabled(has_editable_model);
    relations_action_->setEnabled(has_editable_model);
    family_table_action_->setEnabled(relations_action_->isEnabled());
    file_settings_action_->setEnabled(has_editable_model);
    standard_views_menu_->menuAction()->setEnabled(has_document);
    colors_menu_->menuAction()->setEnabled(has_document);
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_,
                         orthographic_camera_action_, perspective_camera_action_,
                         fly_camera_action_, show_origins_action_, show_points_action_,
                         show_axes_action_, show_planes_action_, show_sketches_action_, show_dimensions_action_}) {
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
    if (workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr) {
        application_actions_[static_cast<std::size_t>(ApplicationMode::Drawing)]
            ->setEnabled(true);
        active_application_ = ApplicationMode::Drawing;
    } else if (workspace_.open_part(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::SheetMetal,
                                ApplicationMode::Surface, ApplicationMode::Piping}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Modeling;
        }
    } else if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::Assembly,
                                ApplicationMode::SheetMetal, ApplicationMode::Surface,
                                ApplicationMode::Piping}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Assembly;
        }
    }
    application_actions_[static_cast<std::size_t>(active_application_)]->setChecked(true);
}

void AssemblyWorkspaceWindow::set_active_application(ApplicationMode mode) {
    const auto index = static_cast<std::size_t>(mode);
    if (index >= application_actions_.size() ||
        !application_actions_[index]->isEnabled()) {
        update_application_actions();
        return;
    }
    active_application_ = mode;
    application_actions_[index]->setChecked(true);
    rebuild_application_toolbar();
}

void AssemblyWorkspaceWindow::rebuild_application_toolbar() {
    if (tools_toolbar_ == nullptr) return;
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
        : active_application_ == ApplicationMode::SheetMetal ? tr("Plech")
        : active_application_ == ApplicationMode::Surface ? tr("Plochy")
        : active_application_ == ApplicationMode::Piping ? tr("Potrubí")
        : tr("Výkres");
    auto* heading = new QLabel(heading_text, tools_toolbar_);
    heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    heading->setFont(tree_->font());
    heading->setStyleSheet(QStringLiteral("font-weight:600; padding:3px;"));
    tools_toolbar_->addWidget(heading);
    const auto add_green_separator = [this] {
        auto* separator = new QWidget(tools_toolbar_);
        separator->setObjectName("greenToolbarSeparator");
        separator->setFixedHeight(1);
        separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        separator->setStyleSheet(
            "QWidget#greenToolbarSeparator { background:#4DD811; border:none; }");
        tools_toolbar_->addWidget(separator);
    };
    const auto add_command = [this](QAction* action) {
        if (action == nullptr) return;
        tools_toolbar_->addAction(action);
        if (auto* button=qobject_cast<QToolButton*>(tools_toolbar_->widgetForAction(action))) {
            new LeftAlignedCommandLabel(button);
            button->setObjectName("applicationCommandButton");
            if (action->menu() != nullptr) {
                button->setPopupMode(QToolButton::InstantPopup);
            }
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            button->setMinimumWidth(146);
        }
    };
    add_green_separator();
    auto* spacing = new QWidget(tools_toolbar_);
    spacing->setFixedHeight(5);
    tools_toolbar_->addWidget(spacing);

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
                        action->setIcon(resource_icon("sketch-dimensions"));
                    }
                    add_command(action);
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
        connect(return_action, &QAction::triggered, this, [this] {
            const std::string displayed = workspace_.displayed_document_id();
            if (workspace_.open_assembly(displayed) == nullptr) return;
            workspace_.activate(displayed);
            active_occurrence_path_.clear();
            active_sketch_id_.clear();
            selected_sketch_id_.clear();
            active_application_ = ApplicationMode::Assembly;
            refresh_tabs();
            refresh_scene();
        });
        add_command(return_action);
        tools_toolbar_->addSeparator();
    }

    if (!active_sketch_id_.empty()) {
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
            add_command(template_region_action_);add_command(template_image_action_);add_green_separator();
        }
        add_command(sketch_normal_view_action_);
        add_command(sketch_flip_view_action_);
        add_command(sketch_rotate_view_action_);
        add_command(selection_action_);
        add_command(sketch_external_reference_action_);
        add_command(sketch_external_profile_action_);
        tools_toolbar_->addSeparator();
        add_command(sketch_trim_action_);
        add_command(sketch_mirror_action_);
        add_command(sketch_offset_action_);
        add_green_separator();
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
        add_green_separator();
        add_command(sketch_constraints_action_);
        add_green_separator();
        add_command(sketch_universal_dimension_action_);
        add_command(sketch_text_action_);
        add_green_separator();
        if(!template_sketch())add_command(finish_sketch_action_);
        if(section_dialog_)add_command(cancel_section_sketch_action_);
        return;
    }

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
            tools_toolbar_->addSeparator();
            if (graph.active_body_id().empty() && (!graph.bodies().empty() ||
                    part->session.document().history_order.empty())) return;
        }
        add_command(selection_action_);
        tools_toolbar_->addSeparator();
        for (auto* action : {construction_point_action_, construction_axis_action_,
                             construction_plane_action_, sketch_action_, curve_3d_action_}) {
            add_command(action);
        }
        add_green_separator();
        add_command(extrusion_action_);
        add_command(revolution_action_);
        add_command(sweep2d_action_);
        add_command(sweep_3d_action_);
        add_command(helical_sweep_action_);
        add_green_separator();
        add_command(fillet_action_);
        add_command(chamfer_action_);
        add_command(shell_action_);
        add_green_separator();
        add_command(thread_action_);
        add_command(shaft_thread_action_);
        add_command(drill_point_action_);
        if(active_body){add_command(mirror_action_);add_command(pattern_action_);}
        add_green_separator();
        for (auto* action : {box_action_, sphere_action_, cylinder_action_, cone_action_,
                             pyramid_action_, wedge_action_}) {
            add_command(action);
        }
        return;
    }
    if (active_application_ == ApplicationMode::Assembly) {
        mirror_action_->setEnabled(!properties_dialog_);add_command(mirror_action_);
        pattern_action_->setEnabled(!properties_dialog_);add_command(pattern_action_);
        add_command(selection_action_);
        tools_toolbar_->addSeparator();
        add_command(insert_action_);
        add_green_separator();
        for (auto* action : {construction_point_action_, construction_axis_action_,
                             construction_plane_action_}) {
            add_command(action);
        }
        add_green_separator();
        add_command(sketch_action_);
        add_command(curve_3d_action_);
        tools_toolbar_->addSeparator();
        add_command(extrusion_action_);
        add_command(revolution_action_);
        add_green_separator();
        return;
    }
    auto* placeholder = new QAction(
        active_application_ == ApplicationMode::SheetMetal
            ? tr("Příkazy plechu – připravuje se")
        : active_application_ == ApplicationMode::Surface
            ? tr("Příkazy ploch – připravuje se")
            : tr("Příkazy potrubí – připravuje se"), tools_toolbar_);
    placeholder->setEnabled(false);
    add_command(placeholder);
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
    const bool command_active = sketch_point_active_ || sketch_segment_active_ ||
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
    set_checked(selection_action_, !command_active);
}

} // namespace zima::app
