#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::start_edge_treatment(
    zima::document::FeatureKind kind) {
    if (properties_dialog_ != nullptr ||
        (kind != zima::document::FeatureKind::Fillet &&
         kind != zima::document::FeatureKind::Chamfer)) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || part->session.document().history.empty()) return;
    if (workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr &&
        !resolve_active_occurrence(part->session.document().document_id)) {
        state_->setText(tr(
            "Fillet/Chamfer vyžaduje přesně aktivovaný výskyt Partu."));
        return;
    }
    edge_treatment_selection_ = kind;
    edge_treatment_hover_seed_.reset();
    viewer_->set_feature_hover_edges({});
    pending_edge_treatment_edges_.clear();
    pending_edge_treatment_groups_.clear();
    pending_edge_treatment_seeds_.clear();
    auto initial = zima::document::PartDocument::create_box_container();
    initial.feature_kind = kind;
    initial.name = kind == zima::document::FeatureKind::Fillet
        ? tr("Zaoblení").toStdString() : tr("Sražení").toStdString();
    initial.edge_treatment.routes.clear();
    edge_treatment_preview_parameters_ = initial.edge_treatment;
    edge_treatment_preview_owner_id_ = initial.id;
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed,
                        std::vector<std::string>) {
            if (committed.edge_treatment.flattened_edges().empty()) {
                throw std::runtime_error("Vyberte alespoň jednu hranu Tělesa");
            }
            auto* target = workspace_.open_part(part_id);
            if (target == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target->session.document();
            next.insert_history_entry(
                zima::document::PartHistoryKind::Feature, committed.id);
            next.history.push_back(std::move(committed));
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    track_tree_edit(dialog);
    edge_treatment_dialog_ = dialog;
    dialog->set_edge_group_callbacks(
        [this](std::size_t group, std::optional<std::size_t> member) {
            remove_edge_treatment_member(group, member);
        },
        [this](std::size_t group) { restore_edge_treatment_route(group); });
    dialog->set_preview_callback([this](const auto& preview) {
        edge_treatment_preview_parameters_ = preview.edge_treatment;
        refresh_edge_treatment_preview();
    });
    refresh_edge_treatment_selection_ui();
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        edge_treatment_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        edge_treatment_hover_seed_.reset();
        pending_edge_treatment_edges_.clear();
        pending_edge_treatment_groups_.clear();
        pending_edge_treatment_seeds_.clear();
        edge_treatment_preview_owner_id_.clear();
        shell_dialog_ = nullptr;
        shell_face_selection_active_ = false;
        pending_shell_faces_.clear();
        drill_point_dialog_ = nullptr;
        drill_point_face_selection_active_ = false;
        pending_drill_point_faces_.clear();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_transient_edges({});
        viewer_->set_transient_dimensions({});
        viewer_->set_edge_treatment_selection_edges({});
        viewer_->set_feature_hover_edges({});
        viewer_->set_candidate_filter({});
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    state_->setText(tr(
        "Vyberte jednu nebo více hran Tělesa. OK operaci vypočítá."));
}

void AssemblyWorkspaceWindow::accept_edge_treatment(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!edge_treatment_selection_) return;
    if (candidate.kind != zima::viewer::CandidateKind::Edge ||
        candidate.geometry != zima::viewer::CandidateGeometry::Display ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte hranu původního solidu aktivního Partu."));
        return;
    }
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly =
        workspace_.open_assembly(workspace_.displayed_document_id());
    if (part == nullptr) return;
    if (assembly == nullptr) {
        if (!candidate.instance_path.empty()) {
            state_->setText(tr("Vyberte hranu aktivního Partu."));
            return;
        }
    } else {
        const auto active_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!active_occurrence || candidate.instance_path != *active_occurrence) {
            state_->setText(tr(
                "Vyberte hranu přesného aktivního výskytu Partu."));
            return;
        }
    }
    const zima::kernel::EdgeReference seed{
        candidate.owner_id, candidate.semantic_key, {}};
    const bool already_selected = std::any_of(
        pending_edge_treatment_groups_.begin(), pending_edge_treatment_groups_.end(),
        [&](const auto& group) {
            return std::find(group.begin(), group.end(), seed) != group.end();
        });
    // Python parity: clicking an already selected route keeps it selected;
    // removal is an explicit action in the dialog's route tree.
    if (already_selected) return;
    auto route = viewer_->tangent_edge_route(candidate);
    for (auto& edge : route) edge.instance_path.clear();
    if (route.empty()) route = {seed};
    std::erase_if(route, [&](const auto& edge) {
        return std::any_of(pending_edge_treatment_groups_.begin(),
            pending_edge_treatment_groups_.end(), [&](const auto& group) {
                return std::find(group.begin(), group.end(), edge) != group.end();
            });
    });
    if (!route.empty()) {
        pending_edge_treatment_groups_.push_back(std::move(route));
        pending_edge_treatment_seeds_.push_back(seed);
    }
    refresh_edge_treatment_selection_ui();
    state_->setText(tr("Vybrané hrany Tělesa: %1. Potvrďte OK.")
        .arg(pending_edge_treatment_edges_.size()));
}

void AssemblyWorkspaceWindow::refresh_edge_treatment_selection_ui() {
    pending_edge_treatment_edges_.clear();
    std::set<zima::viewer::EdgeKey> highlighted;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const std::string instance_path = part == nullptr ? std::string{}
        : resolve_active_occurrence(part->session.document().document_id)
            .value_or(std::string{});
    if (edge_treatment_selection_ && part != nullptr) {
        tree_->setProperty("commandSelectionActive", true);
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Edge,
                                         zima::viewer::CandidateKind::Dimension});
        viewer_->set_candidate_filter(
            [expected_path = instance_path,
             preview_owner = edge_treatment_preview_owner_id_](
                const auto& candidate) {
                if (candidate.kind == zima::viewer::CandidateKind::Dimension) {
                    return candidate.owner_id == preview_owner &&
                        candidate.semantic_key.starts_with("parameter:");
                }
                return candidate.kind == zima::viewer::CandidateKind::Edge &&
                    candidate.geometry ==
                        zima::viewer::CandidateGeometry::Display &&
                    candidate.instance_path == expected_path;
            });
    }
    for (const auto& group : pending_edge_treatment_groups_) {
        for (const auto& edge : group) {
            if (std::find(pending_edge_treatment_edges_.begin(),
                    pending_edge_treatment_edges_.end(), edge) ==
                    pending_edge_treatment_edges_.end()) {
                pending_edge_treatment_edges_.push_back(edge);
                highlighted.insert({edge.owner_id, edge.semantic_key, instance_path});
            }
        }
    }
    if (edge_treatment_dialog_ != nullptr) {
        edge_treatment_dialog_->set_edge_groups(pending_edge_treatment_groups_);
    }
    viewer_->set_edge_treatment_selection_edges(std::move(highlighted));
    refresh_edge_treatment_preview();
}

void AssemblyWorkspaceWindow::refresh_edge_treatment_preview() {
    if (viewer_ == nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!edge_treatment_selection_ || part == nullptr ||
        pending_edge_treatment_groups_.empty()) {
        viewer_->set_transient_edges({});
        viewer_->set_transient_dimensions({});
        return;
    }
    const std::string instance_path = resolve_active_occurrence(
        part->session.document().document_id).value_or(std::string{});
    const auto* displayed_assembly =
        workspace_.open_assembly(workspace_.displayed_document_id());
    const auto decoded_path = instance_path.empty()
        ? std::optional<zima::assembly::InstancePath>{}
        : std::optional{zima::assembly::InstancePath::decode(instance_path)};
    std::vector<std::vector<zima::kernel::ViewerEdge>> display_groups;
    std::vector<zima::kernel::VertexReference> route_start_vertices;
    display_groups.reserve(pending_edge_treatment_groups_.size());
    route_start_vertices.reserve(pending_edge_treatment_groups_.size());
    for (const auto& group : pending_edge_treatment_groups_) {
        std::vector<zima::kernel::ViewerEdge> display_group;
        display_group.reserve(group.size());
        for (const auto& stored : group) {
            auto displayed_reference = stored;
            displayed_reference.instance_path = instance_path;
            auto edge = viewer_->display_edge(displayed_reference);
            if (!edge) continue;
            if (displayed_assembly != nullptr && decoded_path) {
                for (auto& point : edge->points) {
                    point = workspace_.occurrence_point_from_scene(
                        displayed_assembly->session.document().document_id,
                        *decoded_path, point);
                }
                for (auto& side : edge->edge_treatment_side_directions) {
                    for (auto& direction : side) {
                        direction = workspace_.occurrence_direction_from_scene(
                            displayed_assembly->session.document().document_id,
                            *decoded_path, direction);
                    }
                }
            }
            display_group.push_back(std::move(*edge));
        }
        if (!display_group.empty()) {
            zima::kernel::VertexReference route_start;
            const auto paths =
                ordered_edge_treatment_preview_paths(display_group);
            if (paths.size() == 1 && !paths.front().edges.empty() &&
                paths.front().edges.front().
                    edge_treatment_endpoint_references.size() == 2) {
                route_start = paths.front().edges.front().
                    edge_treatment_endpoint_references.front();
                route_start.instance_path.clear();
            }
            route_start_vertices.push_back(std::move(route_start));
            display_groups.push_back(std::move(display_group));
        }
    }
    edge_treatment_preview_parameters_.route_start_vertices =
        route_start_vertices;
    if (edge_treatment_dialog_ != nullptr) {
        edge_treatment_dialog_->set_edge_route_start_vertices(
            std::move(route_start_vertices));
    }
    auto preview = edge_treatment_preview_wire(
        display_groups, edge_treatment_preview_parameters_,
        *edge_treatment_selection_,
        edge_treatment_preview_owner_id_);
    viewer_->set_transient_edges(std::move(preview.edges));
    viewer_->set_transient_dimensions(std::move(preview.dimensions));
}

void AssemblyWorkspaceWindow::remove_edge_treatment_member(
    std::size_t group, std::optional<std::size_t> member) {
    if (group >= pending_edge_treatment_groups_.size()) return;
    if (member && *member < pending_edge_treatment_groups_[group].size()) {
        pending_edge_treatment_groups_[group].erase(
            pending_edge_treatment_groups_[group].begin() + *member);
        if (pending_edge_treatment_groups_[group].empty()) member.reset();
    }
    if (!member) {
        pending_edge_treatment_groups_.erase(
            pending_edge_treatment_groups_.begin() + group);
        if (group < pending_edge_treatment_seeds_.size()) {
            pending_edge_treatment_seeds_.erase(
                pending_edge_treatment_seeds_.begin() + group);
        }
    }
    refresh_edge_treatment_selection_ui();
}

void AssemblyWorkspaceWindow::restore_edge_treatment_route(std::size_t group) {
    if (group >= pending_edge_treatment_seeds_.size()) return;
    const auto& seed = pending_edge_treatment_seeds_[group];
    // Resolve the persisted seed through the same current rollback-body
    // candidate stream used by hover. If it is currently visible, rebuild
    // the complete unambiguous tangent route; otherwise retain the seed.
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const std::string path = part == nullptr ? std::string{}
        : resolve_active_occurrence(part->session.document().document_id)
            .value_or(std::string{});
    zima::viewer::ViewerCandidate candidate;
    candidate.kind = zima::viewer::CandidateKind::Edge;
    candidate.geometry = zima::viewer::CandidateGeometry::Display;
    candidate.owner_id = seed.owner_id;
    candidate.semantic_key = seed.semantic_key;
    candidate.instance_path = path;
    auto route = viewer_->tangent_edge_route(candidate);
    for (auto& edge : route) edge.instance_path.clear();
    pending_edge_treatment_groups_[group] = route.empty()
        ? std::vector{seed} : std::move(route);
    refresh_edge_treatment_selection_ui();
}

bool AssemblyWorkspaceWindow::finish_edge_treatment_selection() {
    return false;
}

void AssemblyWorkspaceWindow::start_shell() {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || part->session.document().history.empty() ||
        part->session.calculated_boundaries().empty() ||
        part->session.calculated_boundaries().back().mesh.triangles.empty()) {
        state_->setText(tr("Shell vyžaduje vypočtené vstupní těleso."));
        return;
    }
    if (workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr &&
        !resolve_active_occurrence(part->session.document().document_id)) {
        state_->setText(tr(
            "Shell vyžaduje přesně aktivovaný výskyt Partu."));
        return;
    }

    auto initial = zima::document::PartDocument::create_shell_container();
    const std::string part_id = part->session.document().document_id;
    pending_shell_faces_.clear();
    shell_face_selection_active_ = true;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed,
                        std::vector<std::string>) {
            auto* target = workspace_.open_part(part_id);
            if (target == nullptr)
                throw std::runtime_error("Part is no longer open");
            auto next = target->session.document();
            next.insert_history_entry(
                zima::document::PartHistoryKind::Feature, committed.id);
            next.history.push_back(std::move(committed));
            auto calculated = calculate_part(next);
            static_cast<void>(
                refresh_sketch_external_references(next, calculated));
            target->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    track_tree_edit(dialog);
    shell_dialog_ = dialog;
    dialog->set_shell_face_callbacks(
        [this](std::size_t index) { remove_shell_face(index); },
        [this] {
            shell_face_selection_active_ = true;
            refresh_shell_selection_ui();
            state_->setText(tr(
                "Vyberte plochy, které mají Shell otevřít."));
        });
    refresh_shell_selection_ui();
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        shell_dialog_ = nullptr;
        shell_face_selection_active_ = false;
        pending_shell_faces_.clear();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    state_->setText(tr(
        "Vyberte plochy, které mají Shell otevřít. MMB výběr ukončí."));
}

void AssemblyWorkspaceWindow::accept_shell_face(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!shell_face_selection_active_ || shell_dialog_ == nullptr) return;
    if (candidate.kind != zima::viewer::CandidateKind::Face ||
        candidate.geometry != zima::viewer::CandidateGeometry::Display ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte plochu vstupního Tělesa."));
        return;
    }
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto expected_path = resolve_active_occurrence(
        part->session.document().document_id).value_or(std::string{});
    if (candidate.instance_path != expected_path) {
        state_->setText(tr("Vyberte plochu přesného aktivního výskytu Partu."));
        return;
    }
    const zima::kernel::FaceReference face{
        candidate.owner_id, candidate.semantic_key, {}};
    const auto found = std::find(
        pending_shell_faces_.begin(), pending_shell_faces_.end(), face);
    if (found == pending_shell_faces_.end()) {
        pending_shell_faces_.push_back(face);
    } else {
        pending_shell_faces_.erase(found);
    }
    refresh_shell_selection_ui();
    state_->setText(tr("Vybrané plochy Shellu: %1.")
        .arg(pending_shell_faces_.size()));
}

void AssemblyWorkspaceWindow::refresh_shell_selection_ui() {
    if (shell_dialog_ == nullptr || viewer_ == nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const std::string instance_path = part == nullptr ? std::string{}
        : resolve_active_occurrence(part->session.document().document_id)
            .value_or(std::string{});
    shell_dialog_->set_shell_faces(pending_shell_faces_);
    shell_dialog_->set_shell_face_selection_active(
        shell_face_selection_active_);

    std::set<zima::viewer::EdgeKey> highlighted_faces;
    for (const auto& face : pending_shell_faces_) {
        highlighted_faces.insert(
            {face.owner_id, face.semantic_key, instance_path});
    }
    viewer_->set_constraint_reference_highlights(
        {}, std::move(highlighted_faces));
    viewer_->clear_selection();
    tree_->setProperty(
        "commandSelectionActive", shell_face_selection_active_);
    if (!shell_face_selection_active_) {
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    viewer_->set_candidate_filter(
        [expected_path = instance_path](const auto& candidate) {
            return candidate.kind == zima::viewer::CandidateKind::Face &&
                candidate.geometry ==
                    zima::viewer::CandidateGeometry::Display &&
                candidate.instance_path == expected_path &&
                !candidate.owner_id.empty() &&
                !candidate.semantic_key.empty();
        });
}

void AssemblyWorkspaceWindow::remove_shell_face(std::size_t index) {
    if (index >= pending_shell_faces_.size()) return;
    pending_shell_faces_.erase(pending_shell_faces_.begin() + index);
    refresh_shell_selection_ui();
}

bool AssemblyWorkspaceWindow::finish_shell_face_selection() {
    if (!shell_face_selection_active_ || shell_dialog_ == nullptr) return false;
    shell_face_selection_active_ = false;
    refresh_shell_selection_ui();
    state_->setText(tr(
        "Výběr ploch Shellu ukončen. Kliknutím do seznamu jej znovu zapnete."));
    return true;
}

void AssemblyWorkspaceWindow::accept_drill_point_face(
        const zima::viewer::ViewerCandidate& candidate) {
    if (!drill_point_face_selection_active_ || drill_point_dialog_ == nullptr)
        return;
    if (candidate.kind != zima::viewer::CandidateKind::Face ||
        (candidate.geometry != zima::viewer::CandidateGeometry::Display &&
         !candidate.semantic_key.starts_with("sweep:cap:")) ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte kruhovou koncovou plochu otvoru."));
        return;
    }
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto expected_path = resolve_active_occurrence(
        part->session.document().document_id).value_or(std::string{});
    if (candidate.instance_path != expected_path) return;
    const zima::kernel::FaceReference face{
        candidate.owner_id, candidate.semantic_key, {}};
    if (std::ranges::find(pending_drill_point_faces_, face) ==
            pending_drill_point_faces_.end()) {
        pending_drill_point_faces_.push_back(face);
    }
    refresh_drill_point_selection_ui();
    state_->setText(tr("Vybraná dna otvorů: %1.")
        .arg(pending_drill_point_faces_.size()));
}

void AssemblyWorkspaceWindow::refresh_drill_point_selection_ui() {
    if (drill_point_dialog_ == nullptr || viewer_ == nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const std::string instance_path = part == nullptr ? std::string{}
        : resolve_active_occurrence(part->session.document().document_id)
            .value_or(std::string{});
    std::vector<QString> labels;
    for (const auto& face : pending_drill_point_faces_) {
        QString label;
        if (part) if (const auto* owner=part->session.document().find_container(face.owner_id))
            label=QString::fromStdString(zima::document::sweep3d_cap_label(*owner,face.semantic_key));
        labels.push_back(std::move(label));
    }
    drill_point_dialog_->set_drill_point_faces(pending_drill_point_faces_, labels);
    drill_point_dialog_->set_drill_point_face_selection_active(
        drill_point_face_selection_active_);
    std::set<zima::viewer::EdgeKey> highlighted_faces;
    for (const auto& face : pending_drill_point_faces_) {
        highlighted_faces.insert(
            {face.owner_id, face.semantic_key, instance_path});
    }
    viewer_->set_constraint_reference_highlights(
        {}, std::move(highlighted_faces));
    viewer_->clear_selection();
    tree_->setProperty("commandSelectionActive",
        drill_point_face_selection_active_);
    if (!drill_point_face_selection_active_) {
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    viewer_->set_candidate_filter(
        [expected_path = instance_path](const auto& candidate) {
            return candidate.kind == zima::viewer::CandidateKind::Face &&
                (candidate.geometry == zima::viewer::CandidateGeometry::Display ||
                 candidate.semantic_key.starts_with("sweep:cap:")) &&
                candidate.instance_path == expected_path &&
                !candidate.owner_id.empty() &&
                !candidate.semantic_key.empty();
        });
}

void AssemblyWorkspaceWindow::remove_drill_point_face(std::size_t index) {
    if (index >= pending_drill_point_faces_.size()) return;
    pending_drill_point_faces_.erase(
        pending_drill_point_faces_.begin() + index);
    refresh_drill_point_selection_ui();
}

bool AssemblyWorkspaceWindow::finish_drill_point_face_selection() {
    if (!drill_point_face_selection_active_ || drill_point_dialog_ == nullptr)
        return false;
    drill_point_face_selection_active_ = false;
    refresh_drill_point_selection_ui();
    state_->setText(tr(
        "Výběr den otvorů ukončen. Kliknutím do seznamu jej znovu zapnete."));
    return true;
}

void AssemblyWorkspaceWindow::apply_extrusion_target_selection_contract() {
    if (extrusion_target_dialog_ == nullptr || viewer_ == nullptr) return;
    viewer_->set_selection_contract({
        zima::viewer::CandidateKind::Face,
        zima::viewer::CandidateKind::Plane});
    const auto* part =
        workspace_.open_part(workspace_.active_document_id());
    const auto expected_path = part == nullptr
        ? std::optional<std::string>{}
        : resolve_active_occurrence(part->session.document().document_id);
    viewer_->set_candidate_filter(
        [path = expected_path.value_or(std::string{}),
         assembly_cut = extrusion_target_assembly_cut_](
            const auto& candidate) {
            bool owned_path = candidate.instance_path == path;
            if (assembly_cut && !candidate.instance_path.empty()) {
                const auto decoded = zima::assembly::InstancePath::decode(
                    candidate.instance_path);
                owned_path = !decoded.occurrence_ids.empty();
            }
            const bool target_kind =
                candidate.kind == zima::viewer::CandidateKind::Face ||
                (candidate.kind == zima::viewer::CandidateKind::Plane &&
                 candidate.semantic_key == "plane");
            // A visible Body fragment is intentionally offered as Display,
            // but it still carries the persisted owner/semantic identity of
            // its original source face.  Use the same stable-reference rule
            // as Container placement; requiring the duplicate hidden
            // OriginalReference packet leaves Part Up-to with no candidates.
            return target_kind &&
                placement_reference_candidate_has_stable_geometry(candidate) &&
                owned_path;
        });
}

void AssemblyWorkspaceWindow::accept_extrusion_target(
    const zima::viewer::ViewerCandidate& candidate) {
    const bool solid_face =
        candidate.kind == zima::viewer::CandidateKind::Face;
    const bool construction_plane =
        candidate.kind == zima::viewer::CandidateKind::Plane &&
        candidate.semantic_key == "plane";
    // Confirmation must consume the exact candidate already offered by the
    // common Viewer list. Visible result fragments are Display packets, but
    // carry the persisted source-face owner/semantic identity and are valid
    // placement references. Requiring OriginalReference here while hover
    // accepts the Display packet makes LMB appear to do nothing.
    const bool stable_target =
        placement_reference_candidate_has_stable_geometry(candidate);
    if (extrusion_target_dialog_ == nullptr ||
        (!solid_face && !construction_plane) ||
        !stable_target ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte rovinnou plochu nebo konstrukční rovinu Partu."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    if (part != nullptr) {
        const auto active_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!active_occurrence || candidate.instance_path != *active_occurrence) {
            state_->setText(tr(
                "Vyberte plochu přesného aktivního výskytu Partu."));
            return;
        }
    } else {
        const auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        if (path.occurrence_ids.empty() ||
            assembly->session.document().find_occurrence(path.occurrence_ids.front()) ==
                nullptr) {
            state_->setText(tr("Vyberte plochu komponenty aktivní sestavy."));
            return;
        }
    }
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 normal;
    bool resolved = false;
    std::vector<zima::kernel::Vec3> surface_triangles;
    QString target_label = tr("Plocha");
    if (part != nullptr) {
        if (const auto* container =
                part->session.document().find_container(candidate.owner_id)) {
            target_label = tr("%1 / plocha").arg(
                QString::fromStdString(container->name));
        }
    }
    const zima::document::ConstructionObject* construction =
        part != nullptr
        ? part->session.document().find_construction(candidate.owner_id)
        : assembly->session.document().find_construction(candidate.owner_id);
    if (construction == nullptr && construction_plane) {
        const auto& constructions = part != nullptr
            ? part->session.document().constructions
            : assembly->session.document().constructions;
        const auto found = std::find_if(
            constructions.begin(), constructions.end(), [&](const auto& value) {
                return value.entity_id == candidate.owner_id;
            });
        if (found != constructions.end()) construction = &*found;
    }
    if (construction != nullptr &&
        construction->kind == zima::document::ConstructionKind::Plane) {
        target_label = QString::fromStdString(construction->name);
        origin = construction->origin;
        normal = construction->direction;
        resolved = true;
    } else {
        // Consume the exact persisted Face packet which produced the offered
        // viewer candidate. Re-resolving it from a document's final body can
        // disagree with creation/rollback presentation and prevented a new
        // additive Extrusion from accepting a source-solid face.
        surface_triangles = viewer_->candidate_face_triangles(candidate);
        for (std::size_t triangle = 0;
             triangle + 2 < surface_triangles.size(); triangle += 3) {
            const auto& first = surface_triangles[triangle];
            const auto& second = surface_triangles[triangle + 1];
            const auto& third = surface_triangles[triangle + 2];
            const zima::kernel::Vec3 a{second.x - first.x, second.y - first.y,
                                       second.z - first.z};
            const zima::kernel::Vec3 b{third.x - first.x, third.y - first.y,
                                       third.z - first.z};
            normal = {a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x};
            const double length = std::sqrt(normal.x * normal.x +
                                            normal.y * normal.y +
                                            normal.z * normal.z);
            if (length <= 1e-12) continue;
            normal = {normal.x / length, normal.y / length, normal.z / length};
            origin = first;
            resolved = true;
            for (const auto& point : surface_triangles) {
                const double distance = (point.x - origin.x) * normal.x +
                    (point.y - origin.y) * normal.y +
                    (point.z - origin.z) * normal.z;
                if (std::abs(distance) > 1e-6) resolved = false;
            }
            break;
        }
    }
    // Up-to a planar solid face follows its underlying geometric plane. The
    // selected face remains the persisted reference owner, but its trimmed
    // boundary must not reject a profile which crosses an edge of that face.
    // This is the expected CAD interaction for e.g. a cylinder whose projected
    // circle only partly overlaps the selected planar face. Curved faces keep
    // their finite persisted triangle packet and exact OCCT target surface.
    if (!resolved && extrusion_target_dialog_->requires_planar_end_target()) {
        state_->setText(tr("Pro zakončení závitu vyberte rovinu nebo rovinnou plochu."));
        return;
    }
    if (!resolved && !surface_triangles.empty()) {
        extrusion_target_dialog_->set_extrusion_surface_target(
            {candidate.owner_id, candidate.semantic_key, candidate.instance_path},
            std::move(surface_triangles), target_label.toStdString());
        finish_extrusion_target_selection();
        state_->setText(tr("Cílová plocha vytažení byla nastavena."));
        return;
    }
    if (!resolved) {
        state_->setText(tr("Vybraná plocha nemá použitelnou geometrii."));
        return;
    }
    extrusion_target_dialog_->set_extrusion_target(
        {candidate.owner_id, candidate.semantic_key, candidate.instance_path},
        origin, normal, target_label.toStdString());
    finish_extrusion_target_selection();
    state_->setText(tr("Cílová plocha vytažení byla nastavena."));
}

void AssemblyWorkspaceWindow::finish_extrusion_target_selection() {
    if (extrusion_target_dialog_ != nullptr)
        extrusion_target_dialog_->finish_extrusion_target_entry();
    extrusion_target_dialog_ = nullptr;
    extrusion_target_assembly_cut_ = false;
    viewer_->clear_selection();
    // Do not auto-resume an incomplete placement row.  The user explicitly
    // entered the Up-to target field; placement may only become active again
    // after an explicit click in its own reference table.
    pending_primitive_reference_index_.reset();
    set_primitive_properties_dimension_selection();
}

} // namespace zima::app
