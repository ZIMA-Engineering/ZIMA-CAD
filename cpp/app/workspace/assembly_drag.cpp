#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::set_selected_component_origin(const std::string& instance_path) {
    if (component_placement_dialog_) return;
    selected_component_origin_path_.clear();
    viewer_->set_component_origin_handle(std::nullopt);
    if (properties_dialog_ || instance_path.empty()) return;
    try {
        const auto path=zima::assembly::InstancePath::decode(instance_path);
        const auto address=workspace_.resolve_occurrence(workspace_.displayed_document_id(),path);
        if (!address || address->owner_assembly_document_id!=workspace_.active_document_id() ||
            path.parent().value_or(zima::assembly::InstancePath{}).encoded()!=active_occurrence_path_) return;
        const auto* owner=workspace_.open_assembly(address->owner_assembly_document_id);
        const auto* occurrence=owner?owner->session.document().find_occurrence(address->occurrence_id):nullptr;
        if (!occurrence || occurrence->derived_copy || occurrence->source_kind==zima::assembly::ComponentSourceKind::Pattern) return;
        selected_component_origin_path_=instance_path;
        viewer_->set_component_origin_handle(zima::viewer::EdgeKey{occurrence->source_document_id+":origin","origin:point",instance_path});
    } catch (const std::exception&) { return; }
}

bool AssemblyWorkspaceWindow::begin_component_drag(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& scene_origin,
    const zima::kernel::Vec3& scene_direction) {
    const auto path=component_placement_dialog_?properties_dialog_instance_path_:selected_component_origin_path_;
    if (path.empty() || candidate.instance_path!=path ||
        (properties_dialog_ && !component_placement_dialog_) ||
        placement_reference_drag_document_ || sketch_drag_document_ || assembly_sketch_drag_document_) return false;
    const auto address=workspace_.resolve_occurrence(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(path));
    if (!address) return false;
    auto* assembly=workspace_.open_assembly(address->owner_assembly_document_id);
    if (!assembly) return false;
    const auto* stored=assembly->session.document().find_occurrence(address->occurrence_id);
    if (!stored) return false;
    const auto pending=component_placement_dialog_?component_placement_dialog_->pending_value():*stored;
    const bool origin_handle = candidate.kind == zima::viewer::CandidateKind::Vertex &&
        candidate.owner_id == pending.source_document_id + ":origin" && candidate.semantic_key == "origin:point";
    if (!origin_handle && (!component_placement_dialog_ || candidate.kind != zima::viewer::CandidateKind::Occurrence)) return false;
    if (pending.grounded || pending.derived_copy) return false;
    auto baseline = assembly->session.document();
    auto* occurrence = baseline.find_occurrence(pending.occurrence_id);
    if (!occurrence) return false;
    *occurrence = pending;
    try { baseline.calculate_placement_references(); }
    catch (const std::exception&) { return false; }
    const auto freedom = baseline.component_constraint_state(pending.occurrence_id);
    if (!freedom.coordinate_free[0] && !freedom.coordinate_free[1] && !freedom.coordinate_free[2]) return false;
    auto ray_origin = scene_origin, ray_direction = scene_direction;
    const auto prefix = zima::assembly::InstancePath::decode(path)
        .parent().value_or(zima::assembly::InstancePath{});
    if (!prefix.occurrence_ids.empty()) {
        ray_origin = workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),prefix,scene_origin);
        ray_direction = workspace_.occurrence_direction_from_scene(workspace_.displayed_document_id(),prefix,scene_direction);
    }
    const double ray_length = std::hypot(ray_direction.x,ray_direction.y,ray_direction.z);
    if (ray_length <= 1e-12) return false;
    component_drag_start_local_origin_ = {occurrence->placement.x,occurrence->placement.y,occurrence->placement.z};
    component_drag_document_ = std::move(baseline);
    component_drag_preview_.reset();
    component_drag_document_id_=address->owner_assembly_document_id;
    component_drag_instance_path_=path;
    component_drag_occurrence_id_ = pending.occurrence_id;
    component_drag_plane_point_ = component_drag_start_local_origin_;
    component_drag_plane_normal_ = {ray_direction.x/ray_length,ray_direction.y/ray_length,ray_direction.z/ray_length};
    const zima::kernel::Vec3 delta{component_drag_plane_point_.x-ray_origin.x,
        component_drag_plane_point_.y-ray_origin.y,component_drag_plane_point_.z-ray_origin.z};
    const double t = delta.x*component_drag_plane_normal_.x + delta.y*component_drag_plane_normal_.y + delta.z*component_drag_plane_normal_.z;
    component_drag_start_hit_ = {ray_origin.x+t*component_drag_plane_normal_.x,
        ray_origin.y+t*component_drag_plane_normal_.y,ray_origin.z+t*component_drag_plane_normal_.z};
    state_->setText(component_placement_dialog_
        ? tr("Tažením přesouváte díl podle volných směrů; polohu potvrdí OK.")
        : tr("Tažením přesouváte díl podle volných směrů; uvolnění myši uloží přesun, Escape jej zruší."));
    return true;
}

void AssemblyWorkspaceWindow::update_component_drag(
    const zima::kernel::Vec3& scene_origin,
    const zima::kernel::Vec3& scene_direction) {
    if (!component_drag_document_) return;
    auto ray_origin = scene_origin, direction = scene_direction;
    const auto prefix = zima::assembly::InstancePath::decode(component_drag_instance_path_)
        .parent().value_or(zima::assembly::InstancePath{});
    if (!prefix.occurrence_ids.empty()) {
        ray_origin = workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),prefix,scene_origin);
        direction = workspace_.occurrence_direction_from_scene(workspace_.displayed_document_id(),prefix,scene_direction);
    }
    const zima::kernel::Vec3 to_plane{component_drag_plane_point_.x-ray_origin.x,
        component_drag_plane_point_.y-ray_origin.y,component_drag_plane_point_.z-ray_origin.z};
    const double denominator = direction.x*component_drag_plane_normal_.x +
        direction.y*component_drag_plane_normal_.y + direction.z*component_drag_plane_normal_.z;
    if (std::abs(denominator)<=1e-12) return;
    const double t=(to_plane.x*component_drag_plane_normal_.x + to_plane.y*component_drag_plane_normal_.y +
        to_plane.z*component_drag_plane_normal_.z)/denominator;
    const zima::kernel::Vec3 delta{ray_origin.x+t*direction.x-component_drag_start_hit_.x,
        ray_origin.y+t*direction.y-component_drag_start_hit_.y,
        ray_origin.z+t*direction.z-component_drag_start_hit_.z};
    const auto allowed = component_drag_document_->component_drag_translation(component_drag_occurrence_id_,delta);
    auto placement = component_drag_document_->find_occurrence(component_drag_occurrence_id_)->placement;
    placement.x += allowed.x; placement.y += allowed.y; placement.z += allowed.z;
    if (component_placement_dialog_) {
        component_placement_dialog_->set_pending_placement(placement);
    } else {
        auto preview=*component_drag_document_;
        preview.find_occurrence(component_drag_occurrence_id_)->placement=placement;
        try {
            preview.calculate_placement_references();
            viewer_->set_mesh(prefix.occurrence_ids.empty()?preview.build_scene():
                workspace_.build_scene_with_assembly_override(workspace_.displayed_document_id(),prefix,preview),false);
            component_drag_preview_=std::move(preview);
        } catch (const std::exception&) { return; }
    }
}

void AssemblyWorkspaceWindow::end_component_drag() {
    // An open dialog owns OK/Cancel. Without a dialog, release is one undoable
    // placement transaction; intermediate pointer samples never enter history.
    const auto path=component_drag_instance_path_;
    bool committed=false;
    if (!component_placement_dialog_ && component_drag_preview_ && component_drag_document_) {
        const auto& start=component_drag_document_->find_occurrence(component_drag_occurrence_id_)->placement;
        const auto& finish=component_drag_preview_->find_occurrence(component_drag_occurrence_id_)->placement;
        if (start!=finish) {
            if (auto* assembly=workspace_.open_assembly(component_drag_document_id_)) {
                assembly->session.commit(std::move(*component_drag_preview_));committed=true;
            }
        }
    }
    component_drag_document_.reset();component_drag_preview_.reset();
    component_drag_occurrence_id_.clear();component_drag_document_id_.clear();component_drag_instance_path_.clear();
    if (committed) {
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();select_occurrence(path);
        set_selected_component_origin(path);
    }
}

} // namespace zima::app
