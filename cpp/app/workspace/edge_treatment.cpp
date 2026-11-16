#include "workspace_internal.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/shell_operations.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/document/profile_targets.hpp>
#include <zima/document/sketch_placement.hpp>
#include "../construction_reference_candidate_policy.hpp"
#include <limits>
#include <cmath>
#include <QUuid>
#include <QScopedValueRollback>

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
    auto initial = zima::document::HistoryContainer{};
    initial.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    initial.feature_id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    initial.feature_parent_id=initial.id;
    initial.container_origin=zima::document::create_container_origin(initial.id);
    initial.feature_kind = kind;
    initial.name = part->session.document().next_numbered_name(
        (kind == zima::document::FeatureKind::Fillet
            ? tr("Zaoblení") : tr("Sražení")).toStdString(), initial.id);
    initial.edge_treatment.routes.clear();
    edge_treatment_preview_parameters_ = initial.edge_treatment;
    edge_treatment_preview_owner_id_ = initial.id;
    edge_treatment_exact_preview_definition_.reset();
    edge_treatment_exact_preview_edges_.clear();
    edge_treatment_exact_preview_generation_=0;
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed,
                        std::vector<std::string>) {
            try {
                static_cast<void>(zima::workspace::commit_edge_treatment(workspace_,kernel_,part_id,std::move(committed),
                    zima::workspace::EdgeTreatmentEditMode::Create, edge_treatment_dialog_->dimension_layouts()));
            } catch(const std::exception& error) { throw std::runtime_error(tr(error.what()).toStdString()); }
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
        edge_treatment_exact_preview_definition_.reset();
        edge_treatment_exact_preview_edges_.clear();
        edge_treatment_exact_preview_generation_=0;
        shell_dialog_ = nullptr;
        shell_face_selection_active_ = false;
        pending_shell_faces_.clear();
        drill_point_dialog_ = nullptr;
        drill_point_face_selection_active_ = false;
        pending_drill_point_faces_.clear();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_transient_edges({});
        viewer_->set_transient_points({});
        viewer_->set_transient_labels({});
        viewer_->set_interactive_labels({});
        viewer_->set_transient_dimensions({});
        viewer_->set_edge_treatment_selection_edges({});
        viewer_->set_feature_hover_edges({});
        viewer_->set_candidate_filter({});
        viewer_->set_candidate_priority({});
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_inspected_faces({});
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
        candidate.owner_id.empty() || candidate.semantic_key.empty() ||
        (viewer_->candidate_filter() && !viewer_->candidate_filter()(candidate))) {
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
            return std::ranges::any_of(group,[&](const auto& selected) {
                return zima::kernel::edge_reference_matches_or_descends_from(
                    seed,selected)||
                    zima::kernel::edge_reference_matches_or_descends_from(
                        selected,seed);
            });
        });
    if (already_selected) {
        for(std::size_t group=0;
                group<pending_edge_treatment_groups_.size();++group) {
            const auto& selected=pending_edge_treatment_groups_[group];
            const auto member=std::ranges::find_if(selected,
                [&](const auto& edge) {
                    return zima::kernel::
                            edge_reference_matches_or_descends_from(seed,edge)||
                        zima::kernel::edge_reference_matches_or_descends_from(
                            edge,seed);
                });
            if(member==selected.end())continue;
            remove_edge_treatment_member(group,
                static_cast<std::size_t>(member-selected.begin()));
            state_->setText(tr(
                "Hrana byla odebrána z výběru zaoblení nebo sražení."));
            return;
        }
        return;
    }
    auto route = zima::document::edge_treatment_intervals_from_seed(
        viewer_->tangent_edge_route(candidate),seed);
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
        // Terminal faces are derived by the ZIMA solver from the current
        // material neighborhood. Do not author hidden per-route end choices
        // while collecting segments.
        edge_treatment_preview_parameters_.route_end_faces.resize(
            pending_edge_treatment_groups_.size());
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
    const auto* input=part==nullptr?nullptr:
        zima::workspace::calculated_operation_input(part->session,
            part->session.document().find_container(
                edge_treatment_preview_owner_id_)
                ?edge_treatment_preview_owner_id_:std::string{});
    if(input&&!pending_edge_treatment_groups_.empty()) {
        auto repaired=edge_treatment_preview_parameters_;
        repaired.routes=pending_edge_treatment_groups_;
        if(zima::document::prune_unresolved_treatment_references(
                repaired,input->mesh)) {
            pending_edge_treatment_groups_=repaired.routes;
            pending_edge_treatment_seeds_.clear();
            for(const auto& route:pending_edge_treatment_groups_)
                if(!route.empty())pending_edge_treatment_seeds_.push_back(
                    route.front());
            edge_treatment_preview_parameters_=std::move(repaired);
        }
    }
    if (edge_treatment_selection_ && part != nullptr) {
        std::set<std::pair<std::string,std::string>> input_edges;
        if(input)for(const auto& edge:input->mesh.edges)
            if(!edge.parameter_seam && edge.reference.valid())
                input_edges.emplace(edge.reference.owner_id,edge.reference.semantic_key);
        tree_->setProperty("commandSelectionActive", true);
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Edge,
                                         zima::viewer::CandidateKind::Dimension});
        // Fillet/Chamfer operates only on exact current Body edges.  Historical
        // authored edges remain inspection geometry and must not be mixed into
        // an automatically collected route.
        viewer_->set_candidate_priority([](const auto& candidate) {
            if(candidate.kind!=zima::viewer::CandidateKind::Edge)return 1;
            return zima::kernel::is_boolean_intersection_interval({
                candidate.owner_id,candidate.semantic_key,{}})?0:1;
        });
        viewer_->set_candidate_filter(
            [expected_path = instance_path,
             input_edges = std::make_shared<const decltype(input_edges)>(std::move(input_edges)),
             preview_owner = edge_treatment_preview_owner_id_](
                const auto& candidate) {
                if (candidate.kind == zima::viewer::CandidateKind::Dimension) {
                    return candidate.owner_id == preview_owner &&
                        candidate.semantic_key.starts_with("parameter:");
                }
                if(candidate.kind!=zima::viewer::CandidateKind::Edge||
                   candidate.instance_path!=expected_path||
                   candidate.geometry!=
                       zima::viewer::CandidateGeometry::Display)return false;
                const auto identity=std::pair{candidate.owner_id,
                    candidate.semantic_key};
                return input_edges->contains(identity);
            }, false);
    }
    auto display_parameters=edge_treatment_preview_parameters_;
    display_parameters.routes=pending_edge_treatment_groups_;
    std::vector<std::vector<zima::kernel::ViewerEdge>> displayed_fragments;
    displayed_fragments.resize(pending_edge_treatment_groups_.size());
    for (std::size_t group_index=0;
            group_index<pending_edge_treatment_groups_.size();++group_index) {
        const auto& authored_group=pending_edge_treatment_groups_[group_index];
        const auto group=zima::document::actual_edge_treatment_intervals(
            authored_group);
        for (const auto& edge : group) {
            if (std::find(pending_edge_treatment_edges_.begin(),
                    pending_edge_treatment_edges_.end(), edge) ==
                    pending_edge_treatment_edges_.end()) {
                pending_edge_treatment_edges_.push_back(edge);
                highlighted.insert({edge.owner_id, edge.semantic_key, instance_path});
            }
        }
        if(input) {
            displayed_fragments[group_index]=
                zima::document::current_treatment_selection_wire(display_parameters,
                    group_index,std::nullopt,input->mesh);
            for(const auto& fragment:displayed_fragments[group_index])
                highlighted.insert({fragment.reference.owner_id,
                    fragment.reference.semantic_key,instance_path});
        }
    }
    if (edge_treatment_dialog_ != nullptr) {
        edge_treatment_dialog_->set_edge_groups(pending_edge_treatment_groups_,
            std::move(displayed_fragments));
    }
    viewer_->set_edge_treatment_selection_edges(std::move(highlighted));
    refresh_edge_treatment_preview();
}

void AssemblyWorkspaceWindow::refresh_edge_treatment_preview() {
    if (viewer_ == nullptr) return;
    // Learning endpoint identities/default node modes notifies the dialog
    // synchronously. The outer refresh consumes those updated values below;
    // a nested refresh would calculate the same draft and node options twice.
    if(edge_treatment_preview_refresh_active_)return;
    QScopedValueRollback refreshing(edge_treatment_preview_refresh_active_,true);
    if (edge_treatment_dialog_) {
        viewer_->set_dimension_layout_editable(true, [this](const auto& reference) {
            return edge_treatment_dialog_ && reference.instance_path == workspace_.active_occurrence_path() &&
                edge_treatment_dialog_->pending_dimension_layout(reference).has_value();
        });
    }
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* input=part==nullptr?nullptr:
        zima::workspace::calculated_operation_input(part->session,
            part->session.document().find_container(
                edge_treatment_preview_owner_id_)
                ?edge_treatment_preview_owner_id_:std::string{});
    if (!edge_treatment_selection_ || part == nullptr ||
        input==nullptr||pending_edge_treatment_groups_.empty()) {
        viewer_->set_transient_edges({});
        viewer_->set_transient_points({});
        viewer_->set_transient_labels({});
        viewer_->set_interactive_labels({});
        viewer_->set_transient_dimensions({});
        edge_treatment_exact_preview_definition_.reset();
        edge_treatment_exact_preview_edges_.clear();
        edge_treatment_exact_preview_generation_=0;
        if(edge_treatment_dialog_)
            edge_treatment_dialog_->set_edge_treatment_preview_error(
                tr(input==nullptr
                    ?"Edge treatment requires a calculated input body."
                    :"Select at least one input edge route."));
        return;
    }
    const std::string instance_path = resolve_active_occurrence(
        part->session.document().document_id).value_or(std::string{});
    std::vector<std::vector<zima::kernel::ViewerEdge>> display_groups;
    std::vector<zima::kernel::Vec3> node_points;
    std::vector<zima::kernel::VertexReference> route_start_vertices;
    const auto authored_start_count=edge_treatment_preview_parameters_.route_start_vertices.size();
    display_groups.reserve(pending_edge_treatment_groups_.size());
    route_start_vertices.reserve(pending_edge_treatment_groups_.size());
    auto display_parameters=edge_treatment_preview_parameters_;
    display_parameters.routes=pending_edge_treatment_groups_;
    for (std::size_t group_index=0;group_index<pending_edge_treatment_groups_.size();++group_index) {
        auto display_group=zima::document::current_treatment_selection_wire(
            display_parameters,group_index,std::nullopt,input->mesh);
        // The shared transient transform places this active-Part wire in its
        // Assembly occurrence. Keep the persisted route endpoints Part-local.
        for(auto& edge:display_group)
            for(auto& endpoint:edge.edge_treatment_endpoint_references)
                endpoint.instance_path.clear();
        const auto append_node=[&](const zima::kernel::Vec3& point) {
            const auto same=[&](const zima::kernel::Vec3& other) {
                return std::hypot(std::hypot(point.x-other.x,
                    point.y-other.y),point.z-other.z)<=1.0e-7;
            };
            if(std::ranges::none_of(node_points,same))
                node_points.push_back(point);
        };
        for(const auto& edge:display_group)if(!edge.points.empty()) {
            append_node(edge.points.front());
            append_node(edge.points.back());
        }
        // Opening Properties must never choose a different R1 merely because
        // the viewer orders edges differently. An unavailable stored endpoint
        // is retained for explicit validation at OK, not silently repaired.
        auto route_start=group_index<edge_treatment_preview_parameters_.route_start_vertices.size()
            ?edge_treatment_preview_parameters_.route_start_vertices[group_index]:zima::kernel::VertexReference{};
        if(!route_start.valid()&&*edge_treatment_selection_==zima::document::FeatureKind::Fillet&&
            edge_treatment_preview_parameters_.fillet_mode==zima::document::EdgeTreatmentParameters::FilletMode::Linear&&!display_group.empty()) {
            const auto paths=ordered_edge_treatment_preview_paths(display_group);
            if(paths.size()==1&&!paths.front().edges.empty()&&paths.front().edges.front().edge_treatment_endpoint_references.size()==2) {
                route_start=paths.front().edges.front().edge_treatment_endpoint_references.front();
                route_start.instance_path.clear();
            }
        }
        route_start_vertices.push_back(std::move(route_start));
        display_groups.push_back(std::move(display_group));
    }
    // Empty optional endpoint slots are presentation data. Opening a constant
    // treatment must not append them to its authored definition and turn an
    // unchanged OK into a model edit. Keep every existing authored slot.
    while(route_start_vertices.size()>authored_start_count&&!route_start_vertices.back().valid())
        route_start_vertices.pop_back();
    edge_treatment_preview_parameters_.route_start_vertices =
        route_start_vertices;
    if (edge_treatment_dialog_ != nullptr) {
        edge_treatment_dialog_->set_edge_route_start_vertices(
            std::move(route_start_vertices));
    }
    const auto preview_end_faces=
        edge_treatment_preview_parameters_.route_end_faces;
    auto preview = edge_treatment_preview_wire(
        display_groups, edge_treatment_preview_parameters_,
        *edge_treatment_selection_,
        edge_treatment_preview_owner_id_, input ? &input->mesh : nullptr);
    if(edge_treatment_dialog_) {
        for(std::size_t route=0;route<pending_edge_treatment_groups_.size();
                ++route) {
            const auto first=std::ranges::find_if(preview.terminal_labels,
                [route](const auto& label) {
                    return label.route==route&&label.first;
                });
            const auto last=std::ranges::find_if(preview.terminal_labels,
                [route](const auto& label) {
                    return label.route==route&&!label.first;
                });
            if(first!=preview.terminal_labels.end()&&
                    last!=preview.terminal_labels.end())
                edge_treatment_dialog_->set_edge_route_end_vertices(route,
                    first->vertex,last->vertex);
        }
        // Physical endpoint identities may swap when the viewer's traversal
        // reverses. The dialog remaps each persisted choice by vertex identity;
        // use that same result for the current calculation preview.
        edge_treatment_preview_parameters_=
            edge_treatment_dialog_->pending_value().edge_treatment;
        // Endpoint identities are learned from the first lightweight wire.
        // Their persisted modes may consequently be remapped by physical
        // vertex identity. Rebuild only when that definition changed so the
        // displayed Cut/RoundedJoin result belongs to the same definition
        // that OK will calculate.
        if(edge_treatment_preview_parameters_.route_end_faces!=
                preview_end_faces)
            preview=edge_treatment_preview_wire(display_groups,
                edge_treatment_preview_parameters_,
                *edge_treatment_selection_,edge_treatment_preview_owner_id_,
                input?&input->mesh:nullptr);

        const auto learned_nodes=edge_treatment_terminal_nodes(
            preview.terminal_labels);
        if(*edge_treatment_selection_==zima::document::FeatureKind::Fillet) {
            // A joined two-branch node always has a spike or a rounding;
            // a stored perpendicular end there becomes the default spike.
            using Mode=zima::document::EdgeTreatmentParameters::RouteEndMode;
            const auto current=edge_treatment_dialog_->pending_value();
            for(const auto& node:learned_nodes) {
                if(node.route_ends.size()<2||node.route_ends.size()>3)continue;
                bool all_no_cut=true;
                bool uniform=true;
                std::optional<Mode> first_mode_seen;
                for(const auto [route,first]:node.route_ends) {
                    if(route>=current.edge_treatment.route_end_faces.size())
                        continue;
                    const auto& ends=
                        current.edge_treatment.route_end_faces[route];
                    const auto mode=first?ends.first_mode:ends.last_mode;
                    if(!first_mode_seen)first_mode_seen=mode;
                    uniform=uniform&&mode==*first_mode_seen;
                    const auto& face=first?ends.first:ends.last;
                    if(std::getenv("ZIMA_CPP_FILLET_NODE_PROFILE"))
                        std::fprintf(stderr,"  end route=%zu first=%d mode=%d face=%d\n",
                            route,first?1:0,static_cast<int>(mode),
                            face.valid()?1:0);
                    all_no_cut=all_no_cut&&(mode==Mode::NoCut||
                        (mode==Mode::Cut&&!face.valid()));
                }
                if(std::getenv("ZIMA_CPP_FILLET_NODE_PROFILE"))
                    std::fprintf(stderr,"Default node %zu ends all_no_cut=%d\n",
                        node.route_ends.size(),all_no_cut?1:0);
                // The ends of one joined node must share one mode; a node
                // whose ends disagree (e.g. after an edge was re-added)
                // falls back to the default spike as a whole.
                if(all_no_cut||!uniform)
                    edge_treatment_dialog_->set_edge_route_end_modes(
                        node.route_ends,Mode::RoundedJoin);
            }
            edge_treatment_preview_parameters_=
                edge_treatment_dialog_->pending_value().edge_treatment;
            if(std::getenv("ZIMA_CPP_FILLET_NODE_PROFILE")) {
                const auto& all=edge_treatment_preview_parameters_
                    .route_end_faces;
                for(std::size_t route=0;route<all.size();++route)
                    std::fprintf(stderr,
                        "  ALL-ENDS route=%zu firstmode=%d lastmode=%d firstface=%d lastface=%d\n",
                        route,static_cast<int>(all[route].first_mode),
                        static_cast<int>(all[route].last_mode),
                        all[route].first.valid()?1:0,
                        all[route].last.valid()?1:0);
            }
        }

        const auto pending=edge_treatment_dialog_->pending_value();
        if(!edge_treatment_exact_preview_definition_||
                    *edge_treatment_exact_preview_definition_!=pending||
                    edge_treatment_exact_preview_generation_!=
                        part->session.data_generation()) {
            edge_treatment_exact_preview_error_.clear();
            try {
                auto exact=zima::workspace::edge_treatment_preview_mesh(
                    part->session,kernel_,pending);
                edge_treatment_exact_preview_edges_=std::move(exact.edges);
                std::erase_if(edge_treatment_exact_preview_edges_,
                    [&](const zima::kernel::ViewerEdge& edge) {
                        return edge.parameter_seam||edge.overlay||
                            edge.construction||std::ranges::find(
                            edge.edge_treatment_owner_ids,pending.id)==
                            edge.edge_treatment_owner_ids.end();
                    });
                if(edge_treatment_exact_preview_edges_.empty())
                    throw std::runtime_error(
                        "Edge treatment preview did not produce an exact result wire.");
                for(auto& edge:edge_treatment_exact_preview_edges_) {
                    // Transient result geometry has one unambiguous azure
                    // presentation; persisted edge colours belong only to
                    // the ordinary calculated scene.
                    edge.color.clear();
                    edge.preview_terminal_dashed=false;
                }
            } catch(const std::exception& error) {
                edge_treatment_exact_preview_edges_.clear();
                edge_treatment_exact_preview_error_=QString::fromUtf8(error.what());
            }
            // A rejected definition is also stable until its inputs change.
            // Keep its error, not a stale successful wire; repaint/inspection
            // must not repeatedly retry the same impossible calculation.
            edge_treatment_exact_preview_definition_=pending;
            edge_treatment_exact_preview_generation_=part->session.data_generation();
        }
        preview.edges=edge_treatment_exact_preview_edges_;
        edge_treatment_dialog_->set_edge_treatment_preview_error(
            edge_treatment_exact_preview_error_.isEmpty()?QString{}:
                tr(edge_treatment_exact_preview_error_.toUtf8().constData()));
    }
    const auto terminal_nodes=edge_treatment_terminal_nodes(
        preview.terminal_labels);
    struct EndMenuProbe {
        zima::document::HistoryContainer key;
        std::vector<std::pair<std::size_t,bool>> route_ends;
        std::uint64_t generation{};
        std::string part_id;
        std::shared_ptr<const int> runtime_identity;
        std::vector<zima::workspace::EdgeTreatmentEndCutChoice> cuts;
        bool join{};
        bool arc{};
    };
    // Joined nodes offer only spike/rounding, never terminal face cuts. Do
    // not enumerate the Cartesian product of unused cut faces while drawing
    // their labels. Free-end cuts are prepared on explicit menu opening.
    const auto probe_end_menu=[this](
            const std::vector<std::pair<std::size_t,bool>>& route_ends,
            const zima::document::HistoryContainer& pending)->EndMenuProbe {
        using Mode=zima::document::EdgeTreatmentParameters::RouteEndMode;
        static std::vector<EndMenuProbe> probe_cache;
        auto probe_key=pending;
        for(const auto [route,first]:route_ends) {
            auto& ends=probe_key.edge_treatment.route_end_faces[route];
            (first?ends.first_mode:ends.last_mode)=Mode::NoCut;
            (first?ends.first:ends.last)={};
        }
        const auto* part=workspace_.open_part(workspace_.active_document_id());
        if(!part)return {};
        for(const auto& cached:probe_cache)
            if(cached.key==probe_key&&cached.route_ends==route_ends&&
               cached.part_id==workspace_.active_document_id()&&
               cached.runtime_identity==part->runtime_identity&&
               cached.generation==part->session.data_generation())
                return cached;
        EndMenuProbe result{probe_key,route_ends,
            part->session.data_generation(),workspace_.active_document_id(),
            part->runtime_identity,{},false,false};
        if(route_ends.size()==1)try {
            result.cuts=zima::workspace::edge_treatment_end_cut_choices(
                part->session,kernel_,pending,route_ends);
        } catch(const std::exception&) {
            result.cuts.clear();
        }
        const bool profile=std::getenv("ZIMA_CPP_FILLET_NODE_PROFILE")!=nullptr;
        if(profile)
            std::fprintf(stderr,"End menu: route_ends=%zu fillet=%d cuts=%zu\n",
                route_ends.size(),
                *edge_treatment_selection_==
                    zima::document::FeatureKind::Fillet?1:0,
                result.cuts.size());
        if(*edge_treatment_selection_==zima::document::FeatureKind::Fillet&&
           route_ends.size()==2) {
            auto rounded=pending;
            auto uncut=pending;
            auto arc=pending;
            const auto set_mode=[&](auto& target,Mode mode) {
                for(const auto [route,first]:route_ends) {
                    auto& ends=target.edge_treatment.route_end_faces[route];
                    (first?ends.first_mode:ends.last_mode)=mode;
                    (first?ends.first:ends.last)={};
                }
            };
            set_mode(rounded,Mode::RoundedJoin);
            set_mode(arc,Mode::RoundedArc);
            set_mode(uncut,Mode::NoCut);
            try {
                const auto without_join=zima::workspace::
                    edge_treatment_preview_mesh(part->session,kernel_,uncut);
                const auto differs=[&](const auto& mesh) {
                    return mesh.vertices!=without_join.vertices||
                        mesh.triangles!=without_join.triangles;
                };
                try {
                    result.join=differs(zima::workspace::
                        edge_treatment_preview_mesh(part->session,kernel_,
                            rounded));
                } catch(const std::exception& error) {
                    if(profile)std::fprintf(stderr,
                        "Rounded join preview failed: %s\n",error.what());
                }
                try {
                    result.arc=differs(zima::workspace::
                        edge_treatment_preview_mesh(part->session,kernel_,
                            arc));
                } catch(const std::exception& error) {
                    if(profile)std::fprintf(stderr,
                        "Arc node preview failed: %s\n",error.what());
                }
                if(profile)std::fprintf(stderr,"Node probe: spike=%d arc=%d\n",
                    result.join?1:0,result.arc?1:0);
            } catch(const std::exception& error) {
                if(profile)std::fprintf(stderr,
                    "Uncut probe failed: %s\n",error.what());
            }
        }
        if(probe_cache.size()>=8)probe_cache.erase(probe_cache.begin());
        probe_cache.push_back(result);
        return result;
    };
    // A joined node whose only possible result is its automatic spike has
    // nothing to choose.
    const auto node_has_choice=[&](
            const std::vector<std::pair<std::size_t,bool>>& route_ends)->bool {
        using Mode=zima::document::EdgeTreatmentParameters::RouteEndMode;
        if(route_ends.size()>=3)return false;
        if(route_ends.size()<2||!edge_treatment_dialog_||
           !edge_treatment_selection_||
           *edge_treatment_selection_!=zima::document::FeatureKind::Fillet)
            return true;
        const auto pending=edge_treatment_dialog_->pending_value();
        for(const auto [route,first]:route_ends) {
            if(route>=pending.edge_treatment.route_end_faces.size())
                return true;
            const auto& ends=pending.edge_treatment.route_end_faces[route];
            if((first?ends.first_mode:ends.last_mode)!=Mode::RoundedJoin)
                return true;
        }
        const auto probed=probe_end_menu(route_ends,pending);
        return !(probed.join&&!probed.arc);
    };
    std::vector<zima::viewer::InteractiveLabel> terminal_labels;
    terminal_labels.reserve(terminal_nodes.size());
    for(const auto& node:terminal_nodes) {
        std::string key="edge-terminal:";
        for(std::size_t index=0;index<node.route_ends.size();++index) {
            if(index!=0)key+=',';
            const auto& [route,first]=node.route_ends[index];
            key+=std::to_string(route)+":"+(first?"first":"last");
        }
        terminal_labels.push_back({node.point,node.text,std::move(key),
            node_has_choice(node.route_ends)});
    }
    viewer_->set_transient_edges(std::move(preview.edges));
    viewer_->set_transient_points(std::move(node_points));
    viewer_->set_transient_labels(std::move(preview.labels));
    viewer_->set_interactive_labels(std::move(terminal_labels),
        [this,probe_end_menu](const std::string& key,
                const QPoint& global_position) {
            constexpr std::string_view prefix="edge-terminal:";
            if(!edge_treatment_dialog_||!key.starts_with(prefix))return false;
            std::vector<std::pair<std::size_t,bool>> route_ends;
            std::size_t begin=prefix.size();
            while(begin<key.size()) {
                const auto comma=key.find(',',begin);
                const auto end=comma==std::string::npos?key.size():comma;
                const auto separator=key.find(':',begin);
                if(separator==std::string::npos||separator>=end)return false;
                std::size_t route{};
                try {
                    route=static_cast<std::size_t>(std::stoull(
                        key.substr(begin,separator-begin)));
                } catch(...) {return false;}
                const auto endpoint=key.substr(separator+1,
                    end-separator-1);
                if(endpoint!="first"&&endpoint!="last")return false;
                route_ends.emplace_back(route,endpoint=="first");
                if(comma==std::string::npos)break;
                begin=comma+1;
            }
            if(route_ends.empty())return false;
            const auto pending=edge_treatment_dialog_->pending_value();
            using Mode=zima::document::EdgeTreatmentParameters::RouteEndMode;
            std::vector<Mode> current;
            for(const auto& [route,first]:route_ends) {
                if(route>=pending.edge_treatment.route_end_faces.size())
                    return false;
                const auto& ends=pending.edge_treatment.route_end_faces[route];
                current.push_back(first?ends.first_mode:ends.last_mode);
            }
            const auto probed=probe_end_menu(route_ends,pending);
            auto cut_choices=probed.cuts;
            const bool rounded_join_valid=probed.join;
            const bool rounded_arc_valid=probed.arc;
            const bool joined_node=route_ends.size()>=2;
            // Špička is the automatic result of a joined node; with no
            // alternative there is nothing to choose.
            if(joined_node&&rounded_join_valid&&!rounded_arc_valid&&
               std::ranges::all_of(current,
                   [](Mode mode){return mode==Mode::RoundedJoin;}))
                return true;
            QMenu menu(this);
            menu.setObjectName("edgeTreatmentEndMenu");
            QAction* rounded_join=rounded_join_valid
                ?menu.addAction(tr("Špička")):nullptr;
            QAction* rounded_arc=rounded_arc_valid
                ?menu.addAction(tr("Zaoblení")):nullptr;
            QAction* no_cut=joined_node&&(rounded_join||rounded_arc)
                ?nullptr:menu.addAction(tr("Bez řezu"));
            std::vector<QAction*> cut_actions;
            cut_actions.reserve(cut_choices.size());
            for(std::size_t index=0;!joined_node&&index<cut_choices.size();
                    ++index)
                cut_actions.push_back(menu.addAction(
                    tr("Řez %1").arg(index+1)));
            if(no_cut)no_cut->setCheckable(true);
            for(auto* action:cut_actions)action->setCheckable(true);
            if(rounded_join)rounded_join->setCheckable(true);
            if(rounded_arc)rounded_arc->setCheckable(true);
            if(rounded_arc)rounded_arc->setChecked(std::ranges::all_of(
                current,[](Mode mode){return mode==Mode::RoundedArc;}));
            if(no_cut)no_cut->setChecked(std::ranges::all_of(current,
                [](Mode mode){return mode==Mode::NoCut;}));
            for(std::size_t choice=0;choice<cut_actions.size();++choice) {
                bool selected=true;
                for(std::size_t index=0;index<route_ends.size();++index) {
                    const auto [route,first]=route_ends[index];
                    const auto& ends=pending.edge_treatment.
                        route_end_faces[route];
                    const auto& stored=first?ends.first:ends.last;
                    // The automatic default Cut has no stored face and is
                    // the first listed choice.
                    selected=selected&&
                        (first?ends.first_mode:ends.last_mode)==Mode::Cut&&
                        (stored==cut_choices[choice].faces[index]||
                            (!stored.valid()&&choice==0));
                }
                cut_actions[choice]->setChecked(selected);
            }
            if(rounded_join)rounded_join->setChecked(std::ranges::all_of(
                current,[](Mode mode){return mode==Mode::RoundedJoin;}));
            const auto* chosen=menu.exec(global_position);
            if(no_cut&&chosen==no_cut)
                edge_treatment_dialog_->set_edge_route_end_modes(
                    route_ends,Mode::NoCut);
            else if(const auto cut=std::ranges::find(cut_actions,chosen);
                    cut!=cut_actions.end()) {
                const auto index=static_cast<std::size_t>(
                    cut-cut_actions.begin());
                edge_treatment_dialog_->set_edge_route_end_choice(
                    route_ends,Mode::Cut,cut_choices[index].faces);
            }
            else if(rounded_join&&chosen==rounded_join)
                edge_treatment_dialog_->set_edge_route_end_modes(
                    route_ends,Mode::RoundedJoin);
            else if(rounded_arc&&chosen==rounded_arc)
                edge_treatment_dialog_->set_edge_route_end_modes(
                    route_ends,Mode::RoundedArc);
            return true;
        });
    for (auto& dimension : preview.dimensions) dimension.reference.instance_path = instance_path;
    viewer_->set_transient_dimensions(std::move(preview.dimensions));
}

void AssemblyWorkspaceWindow::remove_edge_treatment_member(
    std::size_t group, std::optional<std::size_t> member) {
    if (group >= pending_edge_treatment_groups_.size()) return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    if(!part)return;
    const auto* input=zima::workspace::calculated_operation_input(part->session,
        part->session.document().find_container(edge_treatment_preview_owner_id_)?edge_treatment_preview_owner_id_:std::string{});
    if(!input)return;
    auto parameters=edge_treatment_preview_parameters_;
    parameters.routes=pending_edge_treatment_groups_;
    try {zima::document::remove_treatment_selection(parameters,group,member,input->mesh);}
    catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    pending_edge_treatment_groups_=parameters.routes;
    pending_edge_treatment_seeds_.clear();
    for(const auto& route:parameters.routes)pending_edge_treatment_seeds_.push_back(route.front());
    edge_treatment_preview_parameters_=parameters;
    if(edge_treatment_dialog_)edge_treatment_dialog_->set_edge_route_start_vertices(parameters.route_start_vertices);
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
    // Restoration has a persisted identity, not an offered mesh-edge index.
    candidate.geometry_index = std::numeric_limits<std::size_t>::max();
    auto route = zima::document::edge_treatment_intervals_from_seed(
        viewer_->tangent_edge_route(candidate),seed);
    for (auto& edge : route) edge.instance_path.clear();
    if(route.empty())route={seed};
    // Restoring one route must not duplicate edges already assigned elsewhere.
    std::erase_if(route,[&](const auto& edge) {
        for(std::size_t i=0;i<pending_edge_treatment_groups_.size();++i)
            if(i!=group&&std::ranges::find(pending_edge_treatment_groups_[i],edge)!=pending_edge_treatment_groups_[i].end())return true;
        return false;
    });
    if(route.empty())return;
    pending_edge_treatment_groups_[group]=std::move(route);
    if(part)if(const auto* input=zima::workspace::calculated_operation_input(part->session,
        part->session.document().find_container(edge_treatment_preview_owner_id_)?edge_treatment_preview_owner_id_:std::string{})) {
        auto& starts=edge_treatment_preview_parameters_.route_start_vertices;starts.resize(pending_edge_treatment_groups_.size());
        auto parameters=edge_treatment_preview_parameters_;
        parameters.routes=pending_edge_treatment_groups_;
        const auto wire=zima::document::current_treatment_selection_wire(
            parameters,group,std::nullopt,input->mesh);
        const auto ends=zima::workspace::edge_treatment_route_endpoints(
            wire,pending_edge_treatment_groups_[group]);
        if(std::ranges::find(ends,starts[group])==ends.end())starts[group]={};
        if(edge_treatment_dialog_)edge_treatment_dialog_->set_edge_route_start_vertices(starts);
    }
    refresh_edge_treatment_selection_ui();
}

bool AssemblyWorkspaceWindow::finish_edge_treatment_selection() {
    return false;
}

void AssemblyWorkspaceWindow::start_shell(bool surface_only) {
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
    initial.name = part->session.document().next_numbered_name(
        tr(surface_only?"Plochy z tělesa":"Shell").toStdString(),initial.id);
    if(surface_only)initial.shell.thickness=0.;
    const std::string part_id = part->session.document().document_id;
    pending_shell_faces_.clear();
    shell_face_selection_active_ = true;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed,
                        std::vector<std::string>) {
            try {
                static_cast<void>(zima::workspace::commit_shell(workspace_,kernel_,part_id,std::move(committed),
                    zima::workspace::ShellEditMode::Create));
            } catch (const std::exception& error) { throw std::runtime_error(tr(error.what()).toStdString()); }
        }, this);
    properties_dialog_ = dialog;
    track_tree_edit(dialog);
    shell_dialog_ = dialog;
    dialog->set_shell_face_callbacks(
        [this](std::size_t index) { remove_shell_face(index); },
        [this] {
            shell_dialog_->set_shell_source_selection_active(false);
            shell_face_selection_active_ = true;
            refresh_shell_selection_ui();
            state_->setText(shell_dialog_->pending_value().shell.thickness==0.?tr("Vyberte plochy k odstranění. MMB výběr ukončí."):
                tr("Vyberte plochy, které mají Shell otevřít."));
        });
    configure_shell_source_entry(dialog);
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
    state_->setText(dialog->shell_source_selection_active()?tr("Vyberte zdrojové těleso. MMB výběr ukončí."):surface_only?tr("Vyberte plochy k odstranění. MMB výběr ukončí."):
        tr("Vyberte plochy, které mají Shell otevřít. MMB výběr ukončí."));
}

void AssemblyWorkspaceWindow::configure_shell_source_entry(PrimitivePropertiesDialog* dialog) {
    if(dialog->pending_value().shell.thickness!=0.)return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto& source=dialog->pending_value().shell.source_body;
    const auto* body=part?part->session.document().body_history.find(source):nullptr;
    dialog->set_shell_source(source,body?QString::fromStdString(body->name):QString::fromStdString(source));
    dialog->set_shell_source_callbacks([this,dialog] {
        dialog->set_shell_source_selection_active(true);shell_face_selection_active_=true;refresh_shell_selection_ui();
        state_->setText(tr("Vyberte zdrojové těleso. MMB výběr ukončí."));
    },[this,dialog] {
        dialog->set_shell_source({},{});pending_shell_faces_.clear();dialog->set_shell_source_selection_active(true);
        shell_face_selection_active_=true;refresh_shell_selection_ui();
        state_->setText(tr("Vyberte zdrojové těleso. MMB výběr ukončí."));
    },[this] {refresh_shell_selection_ui();});
    if(part&&source.empty()) {
        const auto* input=zima::workspace::calculated_operation_input(part->session,
            part->session.document().find_container(dialog->pending_value().id)?dialog->pending_value().id:std::string{});
        if(!input||!input->has_solid())dialog->set_shell_source_selection_active(true);
    }
}

void AssemblyWorkspaceWindow::accept_shell_face(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!shell_face_selection_active_ || shell_dialog_ == nullptr) return;
    if (candidate.kind != zima::viewer::CandidateKind::Face ||
        candidate.geometry != zima::viewer::CandidateGeometry::Display ||
        candidate.owner_id.empty() || candidate.semantic_key.empty() ||
        (viewer_->candidate_filter() && !viewer_->candidate_filter()(candidate))) {
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
    if(shell_dialog_->shell_source_selection_active()) {
        const auto* body=part->session.document().body_owner_for_object(candidate.owner_id);
        if(!body)return;
        shell_dialog_->set_shell_source(body->scope.id,QString::fromStdString(body->name));
        pending_shell_faces_.clear();shell_dialog_->set_shell_source_selection_active(false);
        refresh_shell_selection_ui();state_->setText(tr("Vyberte plochy k odstranění. MMB výběr ukončí."));return;
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
    state_->setText((shell_dialog_->pending_value().shell.thickness==0.?tr("Vybrané plochy k odstranění: %1."):
        tr("Vybrané plochy Shellu: %1.")).arg(pending_shell_faces_.size()));
}

void AssemblyWorkspaceWindow::refresh_shell_selection_ui() {
    if (shell_dialog_ == nullptr || viewer_ == nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const std::string instance_path = part == nullptr ? std::string{}
        : resolve_active_occurrence(part->session.document().document_id)
            .value_or(std::string{});
    shell_dialog_->set_shell_faces(pending_shell_faces_);
    shell_dialog_->set_shell_face_selection_active(
        shell_face_selection_active_&&!shell_dialog_->shell_source_selection_active());
    const auto pending=shell_dialog_->pending_value();
    const auto* source_body=part?part->session.document().body_history.find(pending.shell.source_body):nullptr;
    shell_dialog_->set_shell_source(pending.shell.source_body,source_body?QString::fromStdString(source_body->name):QString::fromStdString(pending.shell.source_body));

    std::set<zima::viewer::EdgeKey> highlighted_faces;
    for (const auto& face : pending_shell_faces_) {
        highlighted_faces.insert(
            {face.owner_id, face.semantic_key, instance_path});
    }
    if(part&&shell_dialog_->shell_source_inspected())try {
        if(const auto* input=zima::workspace::shell_source_input(*part,part->session.document().find_container(pending.id)?pending.id:std::string{},pending.shell.source_body))
            for(const auto& face:input->mesh.triangle_references)highlighted_faces.insert({face.owner_id,face.semantic_key,instance_path});
    }catch(const std::exception&){}
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
    const bool solid_skin=shell_dialog_->pending_value().shell.thickness==0.;
    std::set<std::pair<std::string,std::string>> input_faces;
    if(part) {
        const auto& id=shell_dialog_->pending_value().id;
        const auto container=part->session.document().find_container(id)?id:std::string{};
        const auto collect=[&](const zima::kernel::BodyResult* input) {
        const zima::kernel::FaceReference* previous{};
        if(input)for(const auto& face:input->mesh.triangle_references) {
            // Triangles are normally grouped by face; do not allocate and
            // search the same key for every triangle of a dense STEP surface.
            if(previous&&previous->owner_id==face.owner_id&&previous->semantic_key==face.semantic_key&&previous->surface_result==face.surface_result)continue;
            previous=&face;
            if(!solid_skin||!face.surface_result)input_faces.emplace(face.owner_id,face.semantic_key);
        }
        };
        if(shell_dialog_->shell_source_selection_active()) {
            for(const auto& body:part->session.document().body_history.bodies())try {
                collect(zima::workspace::shell_source_input(*part,container,body.scope.id));
            }catch(const std::exception&){}
        }else try {collect(zima::workspace::shell_source_input(*part,container,pending.shell.source_body));}catch(const std::exception&){}
    }
    viewer_->set_candidate_filter(
        [expected_path = instance_path,input_faces=std::make_shared<const decltype(input_faces)>(std::move(input_faces))](const auto& candidate) {
            return candidate.kind == zima::viewer::CandidateKind::Face &&
                candidate.geometry ==
                    zima::viewer::CandidateGeometry::Display &&
                candidate.instance_path == expected_path &&
                !candidate.owner_id.empty() &&
                !candidate.semantic_key.empty()&&
                input_faces->contains({candidate.owner_id,candidate.semantic_key});
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
    shell_dialog_->set_shell_source_selection_active(false);
    shell_dialog_->set_shell_source_inspected(false);
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
    const auto* part =
        workspace_.open_part(workspace_.active_document_id());
    const bool points=part&&!extrusion_target_assembly_cut_&&extrusion_target_dialog_->allows_point_end_target();
    const bool skeleton=part&&!extrusion_target_assembly_cut_&&extrusion_target_dialog_->allows_skeleton_end_target();
    std::set<std::string> skeleton_paths;
    if(skeleton)if(const auto* top=workspace_.open_assembly(workspace_.displayed_document_id())) {
        const auto visit=[&](const auto& self,const auto& nodes,const assembly::InstancePath& parent,unsigned depth)->void {
            if(depth>256)return;
            for(const auto& node:nodes) {
                if constexpr(requires{node.nested_snapshot;}) {if(node.suppressed)continue;}
                else {if(node.manually_suppressed||node.dependency_suppressed)continue;}
                const auto path=parent.child(node.occurrence_id);
                if(assembly::is_skeleton(node)&&node.source_document_id!=workspace_.active_document_id())
                    skeleton_paths.insert(path.encoded());
                if constexpr(requires{node.nested_snapshot;})self(self,node.nested_snapshot,path,depth+1);
                else self(self,node.children,path,depth+1);
            }
        };
        visit(visit,top->session.document().components,{},0);
    }
    std::vector<zima::viewer::CandidateKind> kinds{zima::viewer::CandidateKind::Face,zima::viewer::CandidateKind::Plane};
    if(points)kinds.push_back(zima::viewer::CandidateKind::SketchPoint);
    viewer_->set_selection_contract(std::move(kinds));
    const auto expected_path = part == nullptr
        ? std::optional<std::string>{}
        : resolve_active_occurrence(part->session.document().document_id);
    viewer_->set_candidate_filter(
        [this,points,skeleton_paths=std::move(skeleton_paths),path = expected_path.value_or(std::string{}),
         assembly_cut = extrusion_target_assembly_cut_](
            const auto& candidate) {
            bool owned_path = candidate.instance_path == path;
            if (assembly_cut) {
                const auto decoded=zima::assembly::InstancePath::decode(candidate.instance_path);
                const auto prefix=zima::assembly::InstancePath::decode(workspace_.active_occurrence_path());
                owned_path=decoded.occurrence_ids.size()>=prefix.occurrence_ids.size()&&
                    std::equal(prefix.occurrence_ids.begin(),prefix.occurrence_ids.end(),decoded.occurrence_ids.begin());
            }
            if(!owned_path&&(candidate.kind==viewer::CandidateKind::Face||candidate.kind==viewer::CandidateKind::Plane))
                owned_path=skeleton_paths.contains(candidate.instance_path);
            const bool target_kind =
                candidate.kind == zima::viewer::CandidateKind::Face ||
                (points&&candidate.kind==zima::viewer::CandidateKind::SketchPoint) ||
                (candidate.kind == zima::viewer::CandidateKind::Plane &&
                 (candidate.semantic_key == "plane" || candidate.semantic_key.starts_with("origin:plane:")));
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

void AssemblyWorkspaceWindow::apply_sheet_profile_target_selection_contract() {
    if(sheet_profile_target_dialog_==nullptr||viewer_==nullptr)return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    if(part==nullptr)return;
    const auto path=resolve_active_occurrence(
        part->session.document().document_id).value_or(std::string{});
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face,
        zima::viewer::CandidateKind::Plane,
        zima::viewer::CandidateKind::SketchPoint});
    viewer_->set_candidate_filter([path](const auto& candidate) {
        const bool kind=candidate.kind==viewer::CandidateKind::Face||
            candidate.kind==viewer::CandidateKind::SketchPoint||
            (candidate.kind==viewer::CandidateKind::Plane&&
             (candidate.semantic_key=="plane"||
              candidate.semantic_key.starts_with("origin:plane:")));
        return kind&&candidate.instance_path==path&&
            placement_reference_candidate_has_stable_geometry(candidate);
    });
}

void AssemblyWorkspaceWindow::accept_sheet_profile_target(
        const zima::viewer::ViewerCandidate& candidate) {
    auto* dialog=sheet_profile_target_dialog_;
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if(dialog==nullptr||part==nullptr||!sheet_profile_target_frame_)return;
    const auto active_path=resolve_active_occurrence(
        part->session.document().document_id);
    const bool kind=candidate.kind==zima::viewer::CandidateKind::Face||
        candidate.kind==zima::viewer::CandidateKind::SketchPoint||
        (candidate.kind==zima::viewer::CandidateKind::Plane&&
         (candidate.semantic_key=="plane"||
          candidate.semantic_key.starts_with("origin:plane:")));
    if(!kind||!active_path||candidate.instance_path!=*active_path||
       !placement_reference_candidate_has_stable_geometry(candidate)||
       candidate.owner_id.empty()||candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte bod, rovinu nebo rovinnou plochu."));
        return;
    }
    try {
        const auto source=placement_reference_source_candidate(candidate);
        zima::document::ExtrusionParameters::EndTarget requested;
        requested.reference={source.owner_id,source.semantic_key,{}};
        requested.kind=candidate.kind==zima::viewer::CandidateKind::SketchPoint
            ?zima::document::EndTargetKind::Point
            :zima::document::EndTargetKind::Face;
        requested.label=candidate.kind==zima::viewer::CandidateKind::SketchPoint
            ?tr("Bod").toStdString():tr("Plocha").toStdString();
        if(const auto* feature=part->session.document().find_container(
                source.owner_id))
            requested.label=feature->name+" / "+requested.label;
        else for(const auto& sketch:part->session.document().sketches)
            if(sketch.id==source.owner_id) {
                requested.label=sketch.name+" / "+requested.label;break;
            }
        for(const auto& object:part->session.document().constructions)
            if(object.entity_id==source.owner_id||object.id==source.owner_id) {
                requested.label=object.name;break;
            }
        const auto target=zima::document::resolve_profile_target(
            requested,primitive_reference_geometry_);
        if(!target)
            throw std::invalid_argument(
                "Vyberte bod, rovinu nebo rovinnou plochu.");
        auto half_space=zima::document::profile_target_half_space(
            sheet_profile_target_frame_->resolved_origin,
            sheet_profile_target_frame_->resolved_normal,*target);
        if(!half_space) {
            const auto origin=sheet_profile_target_frame_->resolved_origin;
            const auto normal=sheet_profile_target_frame_->resolved_normal;
            const auto forward=zima::document::axis_target_distance(origin,
                normal,*target);
            const zima::kernel::Vec3 reverse{
                -normal.x,-normal.y,-normal.z};
            const auto backward=zima::document::axis_target_distance(origin,
                reverse,*target);
            if(forward&&!backward)half_space=1;
            else if(backward&&!forward)half_space=-1;
        }
        if(!half_space)
            throw std::invalid_argument(
                "Cíl šířky profilu musí ležet mimo rovinu skici a cílová rovina musí být rovnoběžná se skicou.");
        dialog->set_sheet_profile_end_target(*target,half_space);
        finish_sheet_profile_target_selection();
        state_->setText(tr("Cíl šířky profilu byl nastaven."));
    } catch(const std::exception& error) {
        state_->setText(tr(error.what()));
    }
}

void AssemblyWorkspaceWindow::finish_sheet_profile_target_selection() {
    if(sheet_profile_target_dialog_!=nullptr)
        sheet_profile_target_dialog_->finish_sheet_profile_target_entry();
    sheet_profile_target_dialog_=nullptr;
    sheet_profile_target_frame_.reset();
    viewer_->clear_selection();
    pending_primitive_reference_index_.reset();
    set_primitive_properties_dimension_selection();
}

void AssemblyWorkspaceWindow::accept_extrusion_target(
    const zima::viewer::ViewerCandidate& candidate) {
    const bool solid_face =
        candidate.kind == zima::viewer::CandidateKind::Face;
    const bool construction_plane =
        candidate.kind == zima::viewer::CandidateKind::Plane &&
        (candidate.semantic_key == "plane" || candidate.semantic_key.starts_with("origin:plane:"));
    const bool point=!extrusion_target_assembly_cut_&&extrusion_target_dialog_&&extrusion_target_dialog_->allows_point_end_target()&&
        workspace_.open_part(workspace_.active_document_id())&&
        candidate.kind==zima::viewer::CandidateKind::SketchPoint;
    // Confirmation must consume the exact candidate already offered by the
    // common Viewer list. Visible result fragments are Display packets, but
    // carry the persisted source-face owner/semantic identity and are valid
    // placement references. Requiring OriginalReference here while hover
    // accepts the Display packet makes LMB appear to do nothing.
    const bool stable_target =
        placement_reference_candidate_has_stable_geometry(candidate);
    if (extrusion_target_dialog_ == nullptr ||
        (!solid_face && !construction_plane && !point) ||
        !stable_target ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(extrusion_target_dialog_&&extrusion_target_dialog_->allows_point_end_target()
            ?tr("Vyberte bod předchozí skici, rovinu nebo původní plochu.")
            :tr("Vyberte rovinnou plochu nebo konstrukční rovinu Partu."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    if (part != nullptr) {
        const auto active_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!active_occurrence || (candidate.instance_path != *active_occurrence&&!extrusion_target_dialog_->allows_skeleton_end_target())) {
            state_->setText(tr(
                "Vyberte plochu přesného aktivního výskytu Partu."));
            return;
        }
    } else {
        const auto path=zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix=zima::assembly::InstancePath::decode(workspace_.active_occurrence_path());
        if(path.occurrence_ids.size()<prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(),prefix.occurrence_ids.end(),path.occurrence_ids.begin()) ||
            (path.occurrence_ids.size()>prefix.occurrence_ids.size()&&
             !assembly->session.document().find_occurrence(path.occurrence_ids[prefix.occurrence_ids.size()]))) {
            state_->setText(tr("Vyberte plochu komponenty aktivní sestavy."));
            return;
        }
    }
    if(part) {
        try {
            const auto& native=part->session.document();const auto pending=extrusion_target_dialog_->pending_value();
            const auto frame=native.body_owner_for_object(pending.id)?pending.id:native.body_history.active_body_id();
            const auto prepare_feature_target=[&](const zima::document::ExtrusionParameters::EndTarget& target) {
                const bool feature=pending.feature_kind==zima::document::FeatureKind::Feature&&
                    pending.feature.type==zima::document::FeatureType::Modeling;
                const bool extrusion=pending.feature_kind==zima::document::FeatureKind::Extrusion;
                if((!feature&&!extrusion)||(feature&&pending.feature.symmetric)||
                        (extrusion&&pending.extrusion.extent_mode==
                            zima::document::ProfileExtentMode::Symmetric))return;
                // Resolve the pending container and its owned profile through
                // the same cached-reference path as the live preview. Merely
                // adding an offset delta to the committed frame is stale when
                // the support, FRONT/TOP frame or profile offset changed.
                auto preview=native;
                const auto& sketch_id=feature?pending.feature.sketch_id:
                    pending.extrusion.sketch_id;
                auto profile=std::ranges::find(preview.sketches,sketch_id,
                    &zima::sketcher::Sketch::id);
                if(property_owned_sketch_draft_&&property_owned_sketch_draft_->id==sketch_id) {
                    if(profile==preview.sketches.end()) {
                        preview.sketches.push_back(*property_owned_sketch_draft_);
                        profile=std::prev(preview.sketches.end());
                    } else *profile=*property_owned_sketch_draft_;
                }
                if(profile==preview.sketches.end())return;
                profile->owner_container_id=pending.id;
                zima::document::apply_automatic_sketch_work_plane(*profile,
                    pending.placement.references,
                    primitive_reference_geometry_);
                profile->plane_offset=feature?pending.feature.profile_plane_offset:
                    pending.extrusion.profile_plane_offset;
                if(auto* owner=preview.find_container(pending.id))*owner=pending;
                else {
                    preview.insert_history_entry(zima::document::PartHistoryKind::Feature,pending.id);
                    preview.history.push_back(pending);
                }
                preview.resolve_constructions(primitive_reference_geometry_);
                profile=std::ranges::find(preview.sketches,sketch_id,
                    &zima::sketcher::Sketch::id);
                if(profile==preview.sketches.end())return;
                const auto half_space=zima::document::profile_target_half_space(
                    profile->resolved_origin,profile->resolved_normal,target);
                if(!half_space)return;
                const std::size_t side=extrusion_target_dialog_->active_feature_target_side();
                std::optional<int> other_half_space;
                bool other_ambiguous=false;
                const auto& other_targets=feature
                    ?pending.feature.effective_side(1-side).targets
                    :(side?pending.extrusion.end_targets_forward:
                        pending.extrusion.end_targets_reverse);
                for(const auto& authored:other_targets) {
                    const auto resolved=zima::workspace::resolve_profile_end_target(native,
                        part->session.calculated_boundaries(),frame,authored);
                    const auto classified=zima::document::profile_target_half_space(
                        profile->resolved_origin,profile->resolved_normal,resolved);
                    if(!classified||(other_half_space&&*other_half_space!=*classified)) {
                        other_ambiguous=true;break;
                    }
                    other_half_space=classified;
                }
                if(other_ambiguous)other_half_space.reset();
                switch(extrusion_target_dialog_->prepare_feature_up_to_target(
                        *half_space,other_half_space)) {
                case FeatureUpToTargetDecision::SameSideConflict:
                    throw std::invalid_argument(
                        "Cíle obou větví Až k musí ležet na opačných stranách roviny skici.");
                case FeatureUpToTargetDecision::DirectionLocked:
                    throw std::invalid_argument(
                        "Směr prvku je uzamčený a nelze jej otočit k vybranému cíli Až k.");
                default:break;
                }
            };
            zima::document::ExtrusionParameters::EndTarget requested;
            const bool external=candidate.instance_path!=resolve_active_occurrence(native.document_id).value_or(std::string{});
            if(point)requested.kind=zima::document::EndTargetKind::Point;
            requested.label=candidate.semantic_key;
            const auto source=placement_reference_source_candidate(candidate);
            requested.reference={source.owner_id,source.semantic_key,external?source.instance_path:std::string{}};
            if(const auto* feature=native.find_container(candidate.owner_id))requested.label=feature->name+" / "+tr("Plocha").toStdString();
            if(point)for(const auto& sketch:native.sketches)if(sketch.id==candidate.owner_id)requested.label=sketch.name+" / "+tr("Bod").toStdString();
            for(const auto& object:native.constructions)if(object.entity_id==candidate.owner_id || object.id==candidate.owner_id)requested.label=object.name;
            const auto target=external?zima::workspace::prepare_skeleton_end_target(workspace_,native,frame,requested):
                zima::workspace::resolve_profile_end_target(native,part->session.calculated_boundaries(),frame,requested);
            prepare_feature_target(target);
            if(external) {
                extrusion_target_dialog_->set_extrusion_end_target(target);
                finish_extrusion_target_selection(false);state_->setText(tr("Cíl vytažení byl nastaven."));return;
            }
            if(target.kind!=zima::document::EndTargetKind::Face)
                extrusion_target_dialog_->set_extrusion_target(target.reference,target.fallback_origin,target.fallback_normal,target.label,target.kind);
            else {
                if(extrusion_target_dialog_->requires_planar_end_target())throw std::runtime_error("Pro zakončení závitu vyberte rovinu nebo rovinnou plochu.");
                extrusion_target_dialog_->set_extrusion_surface_target(target.reference,target.fallback_triangles,target.label);
            }
            finish_extrusion_target_selection(false);state_->setText(tr("Cíl vytažení byl nastaven."));
        }catch(const std::exception& error){state_->setText(tr(error.what()));}
        return;
    }
    try {
        auto local=zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix=zima::assembly::InstancePath::decode(workspace_.active_occurrence_path());
        local.occurrence_ids.erase(local.occurrence_ids.begin(),
            local.occurrence_ids.begin()+static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        zima::document::ExtrusionParameters::EndTarget requested;
        requested.reference={candidate.owner_id,candidate.semantic_key,local.encoded()};
        requested.label=tr("Plocha").toStdString();
        if(const auto* feature=assembly->session.document().find_cut(candidate.owner_id))requested.label=feature->definition.name;
        else if(const auto* object=assembly->session.document().find_construction(candidate.owner_id))requested.label=object->name;
        // This is the same persisted input packet used by the shared feature
        // preview. It is already in the exact active Assembly's local frame.
        const auto target=zima::document::resolve_profile_target(requested,primitive_reference_geometry_);
        if(!target)throw std::runtime_error("The original extrusion target is unavailable.");
        if(target->kind==zima::document::EndTargetKind::Plane)
            extrusion_target_dialog_->set_extrusion_target(target->reference,target->fallback_origin,target->fallback_normal,target->label);
        else {
            if(extrusion_target_dialog_->requires_planar_end_target())throw std::runtime_error("Pro zakončení závitu vyberte rovinu nebo rovinnou plochu.");
            extrusion_target_dialog_->set_extrusion_surface_target(target->reference,target->fallback_triangles,target->label);
        }
        finish_extrusion_target_selection(false);state_->setText(tr("Cílová plocha vytažení byla nastavena."));
    }catch(const std::exception& error){state_->setText(tr(error.what()));}

}

void AssemblyWorkspaceWindow::finish_extrusion_target_selection(bool clear_inspection) {
    if (extrusion_target_dialog_ != nullptr)
        extrusion_target_dialog_->finish_extrusion_target_entry(clear_inspection);
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
