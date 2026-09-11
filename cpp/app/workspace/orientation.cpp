#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;



void AssemblyWorkspaceWindow::begin_normal_view_selection() {
    if (viewer_ == nullptr || properties_dialog_ != nullptr) return;
    normal_view_selection_active_ = true;
    viewer_->clear_selection();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    state_->setText(tr("Vyberte plochu ve 3D pohledu – pohled se natočí kolmo k ní."));
}

void AssemblyWorkspaceWindow::accept_normal_view_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    normal_view_selection_active_ = false;
    const auto normal = viewer_->candidate_face_normal(candidate);
    viewer_->clear_selection();
    refresh_scene();
    if (!normal) {
        state_->setText(tr("Z vybrané plochy nelze určit normálu."));
        return;
    }
    // Python's _set_view_normal looks along the negative face normal so the
    // face itself faces the camera.
    viewer_->set_view_direction(
        zima::kernel::Vec3{-normal->x, -normal->y, -normal->z});
    viewer_->fit_all();
    state_->setText(tr("Pohled je kolmý k vybrané ploše."));
}



void AssemblyWorkspaceWindow::show_orientation_dialog() {
    if (viewer_ == nullptr || properties_dialog_ != nullptr ||
        orientation_dialog_ != nullptr) return;
    const auto document_id = workspace_.active_document_id();
    std::string named_views_json = "[]";
    if (const auto* part = workspace_.open_part(document_id)) {
        named_views_json = part->session.document().named_views;
    } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
        named_views_json = assembly->session.document().named_views;
    } else {
        return;
    }
    std::vector<zima::app::OrientationSavedView> custom_views;
    try {
        const auto parsed = nlohmann::json::parse(named_views_json);
        if (parsed.is_array()) {
            for (const auto& entry : parsed) {
                if (!entry.is_object() || !entry.contains("name")) continue;
                zima::app::OrientationSavedView view;
                view.name = QString::fromStdString(
                    entry.value("name", std::string()));
                const auto zoom = static_cast<float>(entry.value("zoom", 1.0));
                const std::array<float, 8> state{
                    1.0F, 0.0F, 0.0F, 0.0F, zoom,
                    static_cast<float>(entry.value("pan_x", 0.0)),
                    static_cast<float>(entry.value("pan_y", 0.0)),
                    static_cast<float>(entry.value("reference_scale", zoom))};
                view.camera_state = state;
                custom_views.push_back(std::move(view));
            }
        }
    } catch (const nlohmann::json::exception&) {
        custom_views.clear();
    }
    auto* dialog = new zima::app::OrientationDialog(std::move(custom_views), this);
    orientation_dialog_ = dialog;
    orientation_dialog_original_camera_ = viewer_->camera_state();
    orientation_reference_candidates_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    dialog->set_reference_request_callback([this](std::size_t index) {
        pending_orientation_reference_index_ = index;
        state_->setText(index == 0
            ? tr("Vyberte plochu nebo rovinu pro první směr pohledu.")
            : tr("Vyberte plochu nebo rovinu pro orientační referenci."));
    });
    // Reject a candidate reference whose direction is (anti-)parallel to a
    // reference already accepted in the other row -- e.g. picking TOP twice,
    // or two opposite faces -- mirroring Python's
    // _orientation_references_are_independent().
    dialog->set_independence_check_callback(
        [this](const std::string& existing, const std::string& candidate) {
            const auto existing_it = orientation_reference_candidates_.find(existing);
            const auto candidate_it = orientation_reference_candidates_.find(candidate);
            if (existing_it == orientation_reference_candidates_.end() ||
                candidate_it == orientation_reference_candidates_.end()) {
                return true;
            }
            const auto existing_normal = viewer_->candidate_face_normal(existing_it->second);
            const auto candidate_normal = viewer_->candidate_face_normal(candidate_it->second);
            if (!existing_normal || !candidate_normal) return true;
            const double dot = existing_normal->x * candidate_normal->x +
                existing_normal->y * candidate_normal->y +
                existing_normal->z * candidate_normal->z;
            return std::abs(dot) < 1.0 - 1.0e-8;
        });
    dialog->set_reference_rejected_callback([this] {
        state_->setText(tr(
            "Tuto referenci nelze použít, je rovnoběžná s již zadanou "
            "referencí."));
    });
    const auto apply_rows = [this](
            const std::vector<zima::app::OrientationReferenceRow>& rows) {
        std::vector<std::pair<zima::app::OrientationReferenceRow,
            zima::viewer::ViewerCandidate>> resolved;
        for (const auto& row : rows) {
            const auto found = orientation_reference_candidates_.find(row.reference);
            if (found == orientation_reference_candidates_.end()) continue;
            resolved.emplace_back(row, found->second);
        }
        if (resolved.empty()) return;
        const auto primary_it = std::find_if(resolved.begin(), resolved.end(),
            [](const auto& entry) {
                return entry.first.role == "front" || entry.first.role == "back";
            });
        const auto& primary = primary_it != resolved.end() ? *primary_it : resolved.front();
        auto primary_normal = viewer_->candidate_face_normal(primary.second);
        if (!primary_normal) return;
        zima::kernel::Vec3 normal = *primary_normal;
        const bool reverse_role =
            primary.first.role == "back" || primary.first.role == "bottom" ||
            primary.first.role == "right";
        if (primary.first.flip != reverse_role) {
            normal = {-normal.x, -normal.y, -normal.z};
        }
        double roll_degrees = 0.0;
        const auto secondary_it = std::find_if(resolved.begin(), resolved.end(),
            [&](const auto& entry) {
                return &entry != &primary &&
                    (entry.first.role == "top" || entry.first.role == "bottom" ||
                     entry.first.role == "left" || entry.first.role == "right");
            });
        if (secondary_it != resolved.end()) {
            auto secondary_normal = viewer_->candidate_face_normal(secondary_it->second);
            if (secondary_normal) {
                zima::kernel::Vec3 secondary = *secondary_normal;
                if (secondary_it->first.flip) {
                    secondary = {-secondary.x, -secondary.y, -secondary.z};
                }
                static const std::map<std::string, double> target_angles{
                    {"right", 0.0}, {"top", 90.0}, {"left", 180.0}, {"bottom", -90.0}};
                const auto target = target_angles.find(secondary_it->first.role);
                if (target != target_angles.end()) {
                    roll_degrees = camera_roll_for_direction(
                        {-normal.x, -normal.y, -normal.z}, secondary,
                        target->second);
                }
            }
        }
        viewer_->set_view_direction(
            {-normal.x, -normal.y, -normal.z}, static_cast<float>(roll_degrees));
    };
    dialog->set_rows_changed_callback(apply_rows);
    dialog->set_view_requested_callback(
        [this](const zima::app::OrientationSavedView& view) {
            if (viewer_ == nullptr) return;
            if (!view.standard.empty()) {
                static const std::map<std::string, zima::viewer::StandardView>
                    standard_views{
                        {"default", zima::viewer::StandardView::Isometric},
                        {"front", zima::viewer::StandardView::Front},
                        {"back", zima::viewer::StandardView::Back},
                        {"top", zima::viewer::StandardView::Top},
                        {"bottom", zima::viewer::StandardView::Bottom},
                        {"left", zima::viewer::StandardView::Left},
                        {"right", zima::viewer::StandardView::Right}};
                const auto found = standard_views.find(view.standard);
                if (found != standard_views.end())
                    viewer_->set_standard_view(found->second);
                return;
            }
            viewer_->animate_camera_state(view.camera_state);
        });
    const auto persist_named_views =
        [this, document_id](const nlohmann::json& merged) {
        if (auto* part = workspace_.open_part(document_id)) {
            auto next = part->session.document();
            next.named_views = merged.dump();
            part->session.commit(std::move(next), part->session.calculated_boundaries());
        } else if (auto* assembly = workspace_.open_assembly(document_id)) {
            auto next = assembly->session.document();
            next.named_views = merged.dump();
            assembly->session.commit(std::move(next));
        }
    };
    const auto load_named_views = [this, document_id]() -> nlohmann::json {
        std::string source = "[]";
        if (const auto* part = workspace_.open_part(document_id)) {
            source = part->session.document().named_views;
        } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
            source = assembly->session.document().named_views;
        }
        try {
            auto parsed = nlohmann::json::parse(source);
            if (parsed.is_array()) return parsed;
        } catch (const nlohmann::json::exception&) {
        }
        return nlohmann::json::array();
    };
    dialog->set_save_view_callback(
        [this, dialog, load_named_views, persist_named_views](const QString& name) {
        if (viewer_ == nullptr) return;
        zima::app::OrientationSavedView view;
        view.name = name;
        view.camera_state = viewer_->camera_state();
        dialog->append_saved_view(view);
        auto existing = load_named_views();
        nlohmann::json merged = nlohmann::json::array();
        for (const auto& entry : existing) {
            if (entry.is_object() &&
                entry.value("name", std::string()) != name.toStdString())
                merged.push_back(entry);
        }
        merged.push_back({{"name", name.toStdString()},
            {"pan_x", view.camera_state[5]}, {"pan_y", view.camera_state[6]},
            {"zoom", view.camera_state[4]},
            {"reference_scale", view.camera_state[7]}});
        persist_named_views(merged);
    });
    dialog->set_delete_view_callback(
        [this, load_named_views, persist_named_views](const QString& name) {
        auto existing = load_named_views();
        nlohmann::json merged = nlohmann::json::array();
        for (const auto& entry : existing) {
            if (entry.is_object() &&
                entry.value("name", std::string()) != name.toStdString())
                merged.push_back(entry);
        }
        persist_named_views(merged);
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        orientation_dialog_ = nullptr;
        orientation_reference_candidates_.clear();
        pending_orientation_reference_index_ = 0;
        if (viewer_ != nullptr) {
            viewer_->set_candidate_filter({});
            viewer_->clear_selection();
        }
    });
    connect(dialog, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted && viewer_ != nullptr) {
            viewer_->set_camera_state(orientation_dialog_original_camera_);
        }
    });
    dialog->show();
    state_->setText(tr("Vyberte plochu nebo rovinu pro první směr pohledu."));
}

void AssemblyWorkspaceWindow::accept_orientation_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (orientation_dialog_ == nullptr) return;
    const std::string descriptor =
        candidate.owner_id + ":face:" + std::to_string(
            orientation_reference_candidates_.size());
    orientation_reference_candidates_[descriptor] = candidate;
    const auto label = candidate.semantic_key.starts_with("origin:plane:")
        ? tr("Rovina %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(std::string("origin:plane:").size())).toUpper())
        : tr("Plocha");
    orientation_dialog_->accept_reference(descriptor, label);
    viewer_->clear_selection();
}

} // namespace zima::app
