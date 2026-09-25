#include <QMenu>
#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::clear_selected_sketch_geometry() {
    selected_template_region_.clear();selected_template_image_.clear();selected_symbol_.clear();
    selected_sketch_geometry_ids_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_external_reference_id_.clear();
    selected_sketch_point_id_.clear();
    for (auto* action : {
             sketch_radius_dimension_action_, sketch_diameter_dimension_action_,
             sketch_ellipse_major_dimension_action_,
             sketch_ellipse_minor_dimension_action_,
             sketch_ellipse_rotation_dimension_action_,
             sketch_fix_point_action_}) {
        if (action != nullptr) action->setEnabled(false);
    }
    const bool sketch_tools_available = !active_sketch_id_.empty();
    for (auto* action : {
             sketch_horizontal_action_, sketch_vertical_action_,
                 sketch_universal_dimension_action_,
                 sketch_dimension_action_, sketch_dimension_x_action_,
             sketch_dimension_y_action_, sketch_point_line_dimension_action_,
             sketch_symmetric_dimension_action_,
             sketch_three_point_angle_dimension_action_,
             sketch_angle_dimension_action_}) {
        if (action != nullptr) action->setEnabled(sketch_tools_available);
    }
}

bool AssemblyWorkspaceWindow::delete_selected_sketch_geometry() {
    if(!selected_symbol_.empty()){const auto id=selected_symbol_;remove_symbol(id);return true;}
    if(!selected_template_image_.empty()){const auto id=selected_template_image_;remove_template_image(id);return true;}
    if(!selected_template_region_.empty()){const auto id=selected_template_region_;remove_template_region(id);return true;}
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        sketch_point_active_ || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ || sketch_offset_dialog_ || sketch_mirror_active_ ||
        sketch_circle_active_ ||
        sketch_arc_active_ || sketch_ellipse_active_ ||
        sketch_elliptical_arc_active_ || sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_) return false;
    if (const auto confirmed = viewer_->confirmed_candidate();
        confirmed && confirmed->kind == zima::viewer::CandidateKind::Dimension &&
        confirmed->owner_id == active_sketch_id_ &&
        confirmed->semantic_key.starts_with("dimension:")) {
        remove_sketch_relation(active_sketch_id_,
            confirmed->semantic_key.substr(10), true);
        return true;
    }
    if (const auto confirmed = viewer_->confirmed_candidate();
        confirmed && confirmed->kind ==
                zima::viewer::CandidateKind::SketchConstraint &&
        confirmed->owner_id == active_sketch_id_ &&
        confirmed->semantic_key.starts_with("constraint:")) {
        remove_sketch_relation(active_sketch_id_,
            confirmed->semantic_key.substr(11), false);
        return true;
    }
    if (auto* item = tree_->currentItem(); item != nullptr &&
        item->data(0, Qt::UserRole + 3).toString() ==
            QStringLiteral("part-sketch-dimension")) {
        const auto dimension_id =
            item->data(0, Qt::UserRole).toString().toStdString();
        if (!dimension_id.empty()) {
            remove_sketch_relation(active_sketch_id_, dimension_id, true);
            return true;
        }
    }
    if (auto* item = tree_->currentItem(); item != nullptr &&
        item->data(0, Qt::UserRole + 3).toString() ==
            QStringLiteral("part-sketch-constraint")) {
        const auto constraint_id =
            item->data(0, Qt::UserRole).toString().toStdString();
        if (!constraint_id.empty()) {
            remove_sketch_relation(active_sketch_id_, constraint_id, false);
            return true;
        }
    }
    const std::string geometry_id = !selected_sketch_segment_id_.empty()
        ? selected_sketch_segment_id_
        : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
        : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
        : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
        : !selected_sketch_elliptical_arc_id_.empty()
            ? selected_sketch_elliptical_arc_id_
        : !selected_sketch_bspline_id_.empty() ? selected_sketch_bspline_id_
        : !selected_sketch_text_id_.empty() ? selected_sketch_text_id_
        : selected_sketch_external_reference_id_;
    if (geometry_id.empty() && selected_sketch_point_id_.empty() &&
        selected_sketch_geometry_ids_.empty()) return false;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (!selected_sketch_geometry_ids_.empty()) {
                    const auto contains_geometry = [&](const std::string& id) {
                        const auto contains = [&](const auto& values) {
                            return std::ranges::any_of(values,
                                [&](const auto& value) { return value.id == id; });
                        };
                        return contains(sketch.segments) || contains(sketch.circles) ||
                            contains(sketch.arcs) || contains(sketch.ellipses) ||
                            contains(sketch.elliptical_arcs) ||
                            contains(sketch.bsplines) || contains(sketch.texts) ||
                            contains(sketch.external_references);
                    };
                    // Remove owning geometry before explicitly selected free
                    // points. Geometry removal may already discard its now
                    // unused endpoints, so re-check each point afterwards.
                    for (const auto& id : selected_sketch_geometry_ids_) {
                        if (contains_geometry(id)) sketch.remove_geometry(id);
                    }
                    for (const auto& id : selected_sketch_geometry_ids_) {
                        if (sketch.find_point(id) != nullptr) sketch.remove_point(id);
                    }
                } else if (!geometry_id.empty()) {
                    sketch.remove_geometry(geometry_id);
                } else {
                    sketch.remove_point(selected_sketch_point_id_);
                }
            })) return false;
        clear_selected_sketch_geometry();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Geometrie skici byla odstraněna. Operaci lze vrátit přes Zpět."));
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Geometrii nelze odstranit"), error.what());
        return true;
    }
}

void AssemblyWorkspaceWindow::append_sketch_geometry_role_actions(
    QMenu& menu,const zima::sketcher::Sketch& sketch,const std::string& id) {
    bool auxiliary=false;
    const auto inspect=[&](const auto& values){for(const auto& v:values)if(v.id==id)auxiliary=v.construction;};
    inspect(sketch.points);inspect(sketch.segments);inspect(sketch.circles);inspect(sketch.arcs);
    inspect(sketch.ellipses);inspect(sketch.elliptical_arcs);inspect(sketch.bsplines);
    const bool construction=sketch.geometry_is_centerline(id);
    const auto add=[&](const QString& label,const char* name,auto operation){
        auto* action=menu.addAction(label);action->setObjectName(name);
        connect(action,&QAction::triggered,this,[this,id,operation]{
            try {
                if(!mutate_active_sketch([&](auto& next){operation(next,id);}))return;
                preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
            }catch(const std::exception& e){state_->setText(QObject::tr(e.what()));}
        });return action;
    };
    if(auxiliary)add(tr("Převést na obrys profilu"),"sketchProfileRoleAction",[](auto& s,const auto& id){s.set_geometry_construction(id,false);});
    if(!auxiliary||construction)add(tr("Převést na pomocnou geometrii"),"sketchAuxiliaryRoleAction",[](auto& s,const auto& id){s.set_geometry_construction(id,true);});
    if(!construction)add(tr("Převést na konstrukční geometrii"),"sketchConstructionRoleAction",[](auto& s,const auto& id){s.set_geometry_centerline(id,true);});
    if(construction) {
        const bool visible=sketch.geometry_visible_in_3d(id);
        auto* action=add(tr("Zobrazovat ve 3D"),"sketchGeometryVisible3DAction",[visible](auto& s,const auto& id){s.set_geometry_visible_in_3d(id,!visible);});
        action->setCheckable(true);action->setChecked(visible);
    }
}

void AssemblyWorkspaceWindow::set_active_sketch_geometry_construction(
    const std::string& geometry_id, bool construction) {
    if (active_sketch_id_.empty() || geometry_id.empty()) return;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                sketch.set_geometry_construction(geometry_id, construction);
            })) return;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(construction
            ? tr("Geometrie byla změněna na pomocnou geometrii.")
            : tr("Geometrie byla vrácena do obrysu profilu."));
    } catch (const std::exception& error) {
        state_->setText(QObject::tr(error.what()));
    }
}

void AssemblyWorkspaceWindow::remove_sketch_relation(
    const std::string& sketch_id, const std::string& relation_id,
    bool dimension) {
    if (properties_dialog_ != nullptr || sketch_id.empty() || relation_id.empty()) return;
    if (sketch_id != active_sketch_id_ || active_sketch() == nullptr) return;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (dimension) sketch.remove_dimension(relation_id);
                else sketch.remove_constraint(relation_id);
            })) return;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(dimension
            ? tr("Kóta byla odstraněna. Operaci lze vrátit přes Zpět.")
            : tr("Vazba byla odstraněna. Operaci lze vrátit přes Zpět."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this,
            dimension ? tr("Kótu nelze odstranit") : tr("Vazbu nelze odstranit"),
            error.what());
    }
}

} // namespace zima::app
