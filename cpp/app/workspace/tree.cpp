#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::configure_sketch_box_selection(
    bool sketch_available) {
    const bool drawing_tool_active = sketch_point_active_ ||
        sketch_segment_active_ || sketch_rectangle_active_ ||
        sketch_polygon_active_ || sketch_trim_active_ ||
        sketch_text_active_ ||
        sketch_external_reference_active_ || sketch_circle_active_ ||
        sketch_offset_dialog_ || sketch_mirror_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
        sketch_elliptical_arc_active_ || sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ ||
        sketch_point_dimension_active_ || sketch_universal_dimension_active_ ||
        pending_sketch_dimension_.has_value() ||
        sketch_line_pair_dimension_active_ ||
        sketch_corner_fillet_active_;
    viewer_->set_sketch_box_selection(
        sketch_available && !drawing_tool_active,
        [this](std::vector<zima::viewer::ViewerCandidate> candidates,
               bool additive) {
            const QSignalBlocker tree_signals(tree_);
            if (!additive) {
                clear_selected_sketch_geometry();
                tree_->clearSelection();
            }
            for (const auto& candidate : candidates) {
                if (candidate.owner_id != active_sketch_id_) continue;
                std::optional<std::string> selected_id;
                if (candidate.kind ==
                        zima::viewer::CandidateKind::SketchExternalReference) {
                    if (const auto reference_id =
                            sketch_external_reference_id_from_key(
                                candidate.semantic_key)) {
                        selected_id = *reference_id;
                    }
                } else {
                    const auto separator = candidate.semantic_key.find(':');
                    if (separator != std::string::npos) {
                        selected_id = candidate.semantic_key.substr(separator + 1);
                    }
                }
                if (!selected_id) continue;
                if (!additive ||
                    !selected_sketch_geometry_ids_.erase(*selected_id)) {
                    selected_sketch_geometry_ids_.insert(*selected_id);
                }
            }
            QTreeWidgetItemIterator iterator(tree_);
            while (*iterator != nullptr) {
                auto* item = *iterator;
                const auto role = item->data(0, Qt::UserRole + 3).toString();
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                if (role == QStringLiteral("sketch-geometry") ||
                    role == QStringLiteral("sketch-external-reference")) {
                    item->setSelected(
                        selected_sketch_geometry_ids_.contains(id));
                }
                ++iterator;
            }
            state_->setText(candidates.empty()
                ? tr("Výběrový obdélník neobsahuje žádnou geometrii skici.")
                : tr("Výběrovým obdélníkem bylo vybráno %1 objektů.")
                      .arg(selected_sketch_geometry_ids_.size()));
        });
}

void AssemblyWorkspaceWindow::populate_sketch_tree(
    const zima::sketcher::Sketch& source) {
    const auto& sketch = sketch_trim_active_ && sketch_trim_preview_ && source.id == active_sketch_id_
        ? *sketch_trim_preview_ : source.id == active_sketch_id_ && active_sketch()
            ? *active_sketch() : source;
    tree_->setHeaderLabels({tr("SKETCHER — %1").arg(
        QString::fromStdString(sketch.name))});
    if(sketch.drawing_template) {
        const auto* part=workspace_.open_part(workspace_.active_document_id());
        tree_->setHeaderLabels({QString::fromStdString(part->path.empty()?part->session.document().name:part->path.filename().string())});
        for(const auto& region:sketch.drawing_template->repeat_regions) {
            auto* item=new QTreeWidgetItem(tree_,{tr("Oblast kusovníku")});item->setIcon(0,resource_icon("bom-region"));
            item->setData(0,Qt::UserRole,QString::fromStdString(region.id));item->setData(0,Qt::UserRole+3,"template-repeat-region");
            item->setSelected(selected_template_region_==region.id);
        }
        for(const auto& image:sketch.drawing_template->images) {
            auto* item=new QTreeWidgetItem(tree_,{tr("Obrázek — %1").arg(QString::fromStdString(image.name))});item->setIcon(0,resource_icon("template-image"));
            item->setData(0,Qt::UserRole,QString::fromStdString(image.id));item->setData(0,Qt::UserRole+3,"template-image");
            item->setSelected(selected_template_image_==image.id);
        }
    }
    auto* origin = new QTreeWidgetItem(tree_, {
        tr("Počátek kontejneru — %1").arg(QString::fromStdString(sketch.name))});
    origin->setIcon(0, resource_icon("origin"));
    origin->setFlags(Qt::ItemIsEnabled);
    const std::array origin_children{
        std::pair{QStringLiteral("Lokální počátek"), "point"},
        std::pair{QStringLiteral("X"), "axis"},
        std::pair{QStringLiteral("Y"), "axis"},
        std::pair{QStringLiteral("Z"), "axis"},
        std::pair{QStringLiteral("XY"), "plane"},
        std::pair{QStringLiteral("YZ"), "plane"},
        std::pair{QStringLiteral("XZ"), "plane"}};
    for (std::size_t index = 0; index < origin_children.size(); ++index) {
        const auto& [label, icon] = origin_children[index];
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(icon));
        const QString semantic_key = index == 0
            ? QStringLiteral("external_point:sketch_origin")
            : index == 1 ? QStringLiteral("sketch_axis:x")
            : index == 2 ? QStringLiteral("sketch_axis:y") : QString{};
        if (!semantic_key.isEmpty()) {
            child->setData(0, Qt::UserRole, semantic_key);
            child->setData(0, Qt::UserRole + 3,
                QStringLiteral("sketch-origin-reference"));
            child->setData(0, Qt::UserRole + 4,
                QString::fromStdString(sketch.id));
        } else {
            child->setFlags(Qt::ItemIsEnabled);
        }
    }
    origin->setExpanded(true);

    if (!sketch.external_references.empty()) {
        auto* references = new QTreeWidgetItem(tree_, {tr("Reference")});
        references->setIcon(0, resource_icon("sketch-reference"));
        references->setFlags(Qt::ItemIsEnabled);
        const auto source_display_name = [this](
                const zima::sketcher::SketchExternalReference& reference) {
            QString document_name;
            QString owner_name;
            if (const auto* part = workspace_.open_part(
                    reference.source_document_id)) {
                document_name = QString::fromStdString(
                    part->session.document().name);
                if (const auto* container = part->session.document().find_container(
                        reference.source_owner_id)) {
                    owner_name = QString::fromStdString(container->name);
                } else if (const auto* construction =
                               part->session.document().find_construction(
                                   reference.source_owner_id)) {
                    owner_name = QString::fromStdString(construction->name);
                }
            } else if (const auto* assembly = workspace_.open_assembly(
                           reference.source_document_id)) {
                document_name = QString::fromStdString(
                    assembly->session.document().name);
                if (const auto* construction =
                        assembly->session.document().find_construction(
                            reference.source_owner_id)) {
                    owner_name = QString::fromStdString(construction->name);
                }
            }
            if (document_name.isEmpty()) {
                return QString::fromStdString(reference.source_owner_id);
            }
            return owner_name.isEmpty() || owner_name == document_name
                ? document_name
                : tr("%1 — %2").arg(document_name, owner_name);
        };
        for (const auto& reference : sketch.external_references) {
            const QString kind = reference.kind ==
                    zima::sketcher::ExternalReferenceKind::Face ? tr("plocha")
                : reference.kind == zima::sketcher::ExternalReferenceKind::Edge
                    ? tr("hrana")
                : reference.kind == zima::sketcher::ExternalReferenceKind::Axis
                    ? tr("osa") : tr("bod");
            auto* item = new QTreeWidgetItem(references, {
                source_display_name(reference) +
                QStringLiteral(" · ") + kind +
                (reference.broken ? tr(" — přerušená") : QString{})});
            item->setIcon(0, resource_icon("sketch-reference"));
            item->setData(0, Qt::UserRole, QString::fromStdString(reference.id));
            item->setData(0, Qt::UserRole + 3, "sketch-external-reference");
            item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
            item->setSelected(reference.id == selected_sketch_external_reference_id_);
        }
        references->setExpanded(true);
    }

    auto* geometry = new QTreeWidgetItem(tree_, {tr("Geometrie")});
    geometry->setIcon(0, resource_icon("sketch"));
    geometry->setFlags(Qt::ItemIsEnabled);
    auto* constraints = new QTreeWidgetItem(tree_, {tr("Vazby")});
    constraints->setIcon(0, resource_icon("sketch-constraints"));
    constraints->setFlags(Qt::ItemIsEnabled);
    auto* dimensions = new QTreeWidgetItem(tree_, {tr("Kóty")});
    dimensions->setIcon(0, resource_icon("sketch-dimensions"));
    dimensions->setFlags(Qt::ItemIsEnabled);
    const auto geometry_item = [&](const std::string& id, const QString& label,
                                   const QString& icon) {
        auto* item = new QTreeWidgetItem(geometry, {label});
        item->setIcon(0, resource_icon(icon));
        item->setData(0, Qt::UserRole, QString::fromStdString(id));
        item->setData(0, Qt::UserRole + 3, "sketch-geometry");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        return item;
    };
    const auto indexed_geometry_label = [&](const QString& kind, int index,
                                            bool construction) {
        const auto indexed = kind + QStringLiteral("%1").arg(
            index, 3, 10, QLatin1Char('0'));
        return construction
            ? tr("Pomocná geometrie · %1").arg(indexed)
            : indexed;
    };
    int point_index = 0;
    std::unordered_map<std::string, QString> point_labels;
    for (const auto& point : sketch.points) {
        const QString label = indexed_geometry_label(
            tr("Bod"), ++point_index, point.construction);
        point_labels.emplace(point.id, label);
        auto* item = geometry_item(point.id, label, "point");
        item->setSelected(point.id == selected_sketch_point_id_);
    }
    int segment_index = 0;
    for (const auto& segment : sketch.segments) {
        const int index = ++segment_index;
        const QString segment_label = segment.centerline
            ? tr("Konstrukční čára%1").arg(
                index, 3, 10, QLatin1Char('0'))
            : indexed_geometry_label(tr("Úsečka"), index,
                segment.construction);
        auto* item = geometry_item(segment.id,
            segment_label,
            "sketch");
        item->setSelected(segment.id == selected_sketch_segment_id_);
        const auto append_endpoint = [&](const std::string& point_id,
                                         const QString& role) {
            const auto label = point_labels.find(point_id);
            auto* endpoint = new QTreeWidgetItem(item, {
                tr("%1 — %2").arg(role,
                    label == point_labels.end()
                        ? tr("Chybějící bod") : label->second)});
            endpoint->setIcon(0, resource_icon("point"));
            endpoint->setData(
                0, Qt::UserRole, QString::fromStdString(point_id));
            endpoint->setData(0, Qt::UserRole + 3, "sketch-geometry");
            endpoint->setData(
                0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        };
        append_endpoint(segment.first_point_id, tr("Počáteční bod"));
        append_endpoint(segment.second_point_id, tr("Koncový bod"));
    }
    int circle_index = 0;
    for (const auto& circle : sketch.circles) {
        auto* item = geometry_item(circle.id,
            indexed_geometry_label(tr("Kružnice"), ++circle_index,
                circle.construction), "sketch");
        item->setSelected(circle.id == selected_sketch_circle_id_);
    }
    int arc_index = 0;
    for (const auto& arc : sketch.arcs) geometry_item(arc.id,
        indexed_geometry_label(tr("Oblouk"), ++arc_index, arc.construction), "sketch")
            ->setSelected(arc.id == selected_sketch_arc_id_);
    int ellipse_index = 0;
    for (const auto& ellipse : sketch.ellipses) geometry_item(ellipse.id,
        indexed_geometry_label(tr("Elipsa"), ++ellipse_index,
            ellipse.construction), "sketch")
            ->setSelected(ellipse.id == selected_sketch_ellipse_id_);
    int elliptical_arc_index = 0;
    for (const auto& arc : sketch.elliptical_arcs) geometry_item(arc.id,
        indexed_geometry_label(tr("Eliptický oblouk"),
            ++elliptical_arc_index, arc.construction), "sketch")
            ->setSelected(arc.id == selected_sketch_elliptical_arc_id_);
    int spline_index = 0;
    for (const auto& spline : sketch.bsplines) geometry_item(spline.id,
        indexed_geometry_label(sketch.find_offset(spline.id) ? (sketch.find_offset(spline.id)->broken?tr("Offset — neplatný"):tr("Offset")) : spline.interpolating
                ? tr("Interpolační spline") : tr("B-spline"), ++spline_index,
            spline.construction), "sketch")
            ->setSelected(spline.id == selected_sketch_bspline_id_);
    int text_index = 0;
    for (const auto& text : sketch.texts) geometry_item(text.id,
        tr("Text%1").arg(++text_index, 3, 10, QLatin1Char('0')), "sketch")
            ->setSelected(text.id == selected_sketch_text_id_);
    int constraint_index = 0;
    for (const auto& constraint : sketch.constraints) {
        auto* item = new QTreeWidgetItem(constraints, {
            sketch_constraint_label(constraint.kind) +
            QStringLiteral("%1").arg(++constraint_index, 3, 10, QLatin1Char('0'))});
        item->setIcon(0, sketch_constraint_tree_icon(constraint.kind));
        item->setData(0, Qt::UserRole, QString::fromStdString(constraint.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch-constraint");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
    }
    int dimension_index = 0;
    for (const auto& dimension : sketch.dimensions) {
        auto* item = new QTreeWidgetItem(dimensions, {
            sketch_dimension_label(dimension) + QStringLiteral(" · %1")
                .arg(++dimension_index, 3, 10, QLatin1Char('0'))});
        item->setIcon(0, resource_icon("sketch-dimensions"));
        item->setData(0, Qt::UserRole, QString::fromStdString(dimension.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch-dimension");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
    }
    geometry->setExpanded(true);
    constraints->setExpanded(true);
    dimensions->setExpanded(true);
}

bool AssemblyWorkspaceWindow::part_history_insertion_allowed() const {
    // A hidden parent dialog still owns the transaction during Point/Sketch
    // editing. Do not restore history insertion until that whole command ends.
    return !properties_dialog_ && !tree_edit_dialog_ && !pending_profile_feature_ &&
        active_sketch_id_.empty();
}

void AssemblyWorkspaceWindow::track_tree_edit(QDialog* dialog) {
    bind_local_origin_selection(dialog);
    // Keep the outer transaction while its Point/Sketch sub-editor is open.
    if (tree_edit_dialog_ || !dialog) return;
    tree_edit_dialog_ = dialog;
    tree_edit_document_id_ = workspace_.active_document_id();
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (tree_edit_dialog_ != dialog) return;
        tree_edit_dialog_.clear();
        tree_edit_document_id_.clear();
        tree_edit_sketch_container_.reset();
        // The command's normal cleanup restores the committed Tree and View.
    });
    QTimer::singleShot(0, this, [this] {
        if (!tree_edit_dialog_) return;
        // Project only the Tree. A full scene refresh here would erase the
        // reference picker installed by the command after opening Properties.
        const QSignalBlocker blocked(tree_);
        QTreeWidgetItemIterator rows(tree_);
        while (*rows) {
            auto* row = *rows++;
            if (row->data(0, Qt::UserRole + 3).toString() == "document-origin" &&
                row->data(0, Qt::UserRole).toString().toStdString() == tree_edit_document_id_ + ":origin" &&
                row->data(0, Qt::UserRole + 1).toString().toStdString() == active_occurrence_path_) {
                const bool assembly = workspace_.open_assembly(tree_edit_document_id_) != nullptr;
                add_pending_tree_item(row->parent(), tree_edit_document_id_,
                    zima::assembly::InstancePath::decode(active_occurrence_path_), assembly);
                if (!assembly) {
                    // Editing another body's row must also retire the active
                    // body's marker, even when the command needed no rebuild.
                    std::vector<QTreeWidgetItem*> markers;
                    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
                        const auto role = (*it)->data(0, Qt::UserRole + 3).toString();
                        if (role == "part-insert-here" || role == "part-body-insert-here")
                            markers.push_back(*it);
                    }
                    for (auto* marker : markers) delete marker;
                }
                return;
            }
        }
    });
}

void AssemblyWorkspaceWindow::add_pending_tree_item(QTreeWidgetItem* parent,
    const std::string& document_id,
    const zima::assembly::InstancePath& instance_path, bool assembly) {
    if (document_id != workspace_.active_document_id() ||
        instance_path.encoded() != active_occurrence_path_) return;
    std::optional<zima::document::HistoryContainer> feature;
    std::optional<zima::document::ConstructionObject> construction;
    std::optional<zima::sketcher::Sketch> pending_sketch;
    if (tree_edit_dialog_ && tree_edit_document_id_ == document_id) {
        if (auto* dialog = dynamic_cast<PrimitivePropertiesDialog*>(tree_edit_dialog_.data()))
            feature = dialog->pending_value();
        else if (auto* dialog = dynamic_cast<SweepPlacementDialog*>(tree_edit_dialog_.data()))
            feature = dialog->pending;
        else if (auto* dialog = dynamic_cast<ShaftThreadDialog*>(tree_edit_dialog_.data()))
            feature = dialog->pending();
        else if (auto* dialog = dynamic_cast<SketchPropertiesDialog*>(tree_edit_dialog_.data())) {
            auto value = dialog->pending_value();
            pending_sketch = std::move(value.first);
            feature = tree_edit_sketch_container_;
            if (feature) { feature->name = pending_sketch->name; feature->placement = value.second; }
        }
        else if (auto* dialog = dynamic_cast<ConstructionPropertiesDialog*>(tree_edit_dialog_.data())) {
            if (dialog->is_sweep()) feature = dialog->pending_sweep_value();
            else construction = dialog->pending_value();
        }
    } else if (pending_profile_feature_) {
        feature = *pending_profile_feature_;
    }
    if (!feature && !construction && !pending_sketch) return;
    auto* path = feature && feature->feature_kind == zima::document::FeatureKind::Sweep3D
        ? &feature->sweep3d.path : construction ? &*construction : nullptr;
    // A nested Point is still pending in its own dialog until its OK.
    if (path && construction_parameter_preview_ &&
        construction_parameter_preview_->parent_construction_id == path->id) {
        const auto& point = *construction_parameter_preview_;
        const auto found = std::ranges::find(path->curve_points, point.id,
            &zima::document::ConstructionObject::id);
        if (found == path->curve_points.end()) path->curve_points.push_back(point);
        else *found = point;
    }
    const auto& id = feature ? feature->id : construction ? construction->id : pending_sketch->id;
    const auto& name = feature ? feature->name : construction ? construction->name : pending_sketch->name;
    // A full rebuild groups draft rows into bodies after this projection.
    // The deferred dialog update sees that grouped tree and must update the
    // same body row, rather than append a second draft at document level.
    if (!assembly) if (const auto* part = workspace_.open_part(document_id)) {
        const auto& graph = part->session.document().body_history;
        const auto* owner = graph.owner(id);
        const auto& body_id = owner ? owner->scope.id : sketch_properties_body_id_.empty()
            ? graph.active_body_id() : sketch_properties_body_id_;
        for (int index = 0; index < parent->childCount(); ++index) {
            auto* body = parent->child(index);
            if (body->data(0, Qt::UserRole + 3).toString() == "part-body" &&
                body->data(0, Qt::UserRole).toString().toStdString() == body_id) {
                parent = body;
                break;
            }
        }
    }
    QTreeWidgetItem* row = nullptr;
    int insertion = parent->childCount();
    for (int i = parent->childCount() - 1; i >= 0; --i) {
        auto* child = parent->child(i);
        const auto role = child->data(0, Qt::UserRole + 3).toString();
        if (role == "part-insert-here" || role == "assembly-insert-here") {
            insertion = i;
            delete parent->takeChild(i);
        } else if (child->data(0, Qt::UserRole).toString().toStdString() == id) {
            row = child;
        }
    }
    if (!row) {
        row = new QTreeWidgetItem;
        parent->insertChild(std::min(insertion, parent->childCount()), row);
    } else {
        qDeleteAll(row->takeChildren());
    }
    row->setText(0, QString::fromStdString(name));
    row->setData(0, Qt::UserRole, QString::fromStdString(id));
    row->setData(0, Qt::UserRole + 1, QString::fromStdString(instance_path.encoded()));
    row->setData(0, Qt::UserRole + 3, feature
        ? (assembly ? "assembly-cut" : "part-container")
        : construction ? (assembly ? "assembly-construction" : "part-construction")
        : (assembly ? "assembly-sketch" : "part-sketch"));
    row->setData(0, Qt::UserRole + 12, true);
    row->setForeground(0, QBrush(QColor(70, 190, 95)));
    auto font = row->font(0);
    font.setBold(true);
    font.setItalic(false);
    font.setStrikeOut(false);
    row->setFont(0, font);
    if (feature) {
        row->setIcon(0, resource_icon(feature_icon_name(feature->feature_kind)));
        const zima::sketcher::Sketch* sketch = nullptr;
        const auto find_sketch = [&](const auto& document) {
            const auto found = std::ranges::find(document.sketches, id,
                &zima::sketcher::Sketch::owner_container_id);
            if (found != document.sketches.end()) sketch = &*found;
        };
        if (const auto* part = workspace_.open_part(document_id)) find_sketch(part->session.document());
        else if (const auto* source = workspace_.open_assembly(document_id)) find_sketch(source->session.document());
        if (pending_sketch) sketch = &*pending_sketch;
        add_history_container_tree_children(row, *feature, instance_path, sketch, assembly);
    } else if (construction) {
        row->setIcon(0, resource_icon(construction->kind == zima::document::ConstructionKind::Curve3D
            ? "sketch-3d" : construction->kind == zima::document::ConstructionKind::Point
                ? "point" : construction->kind == zima::document::ConstructionKind::Axis ? "axis" : "plane"));
        add_construction_tree_children(row, *construction, instance_path);
    } else {
        row->setIcon(0, resource_icon("sketch"));
    }
    row->setExpanded(true);
}

void AssemblyWorkspaceWindow::add_part_tree_children(
    QTreeWidgetItem* parent,
    const zima::document::PartDocument& document) {
    // A brand-new Extrusion/Revolution temporarily places its owned Sketch
    // and draft owner in DocumentSession while the Sketcher sub-editor is
    // active. Show it exactly once through the pending Tree projection below.
    // Existing containers being edited remain visible at the rollback boundary.
    const std::string pending_creation_id =
        pending_profile_feature_ && !pending_profile_transform_original_
        ? pending_profile_feature_->id : std::string{};
    const auto construction_path = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    zima::workspace::ReferenceIndex references;
    if (const auto* part=workspace_.open_part(document.document_id)) {
        const auto& boundaries=part->session.calculated_boundaries();
        if (!boundaries.empty()) references.add_geometry(boundaries.back().mesh.original_references);
    }
    references.add_geometry(document.origin_viewer_mesh().original_references);
    references.add_geometry(document.body_origin_reference_geometry());
    references.add_geometry(document.history_origin_reference_geometry_before(""));
    references.add_geometry(document.construction_viewer_mesh().original_references);
    add_origin_tree_item(parent, document.document_id, false, construction_path);
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        if (!pending_creation_id.empty() &&
            container.id == pending_creation_id) continue;
        const QString operation = container.combine_mode ==
                zima::document::CombineMode::Subtract
            ? QStringLiteral("− ") : QStringLiteral("+ ");
        auto* item = new QTreeWidgetItem(parent,
            {operation + QString::fromStdString(container.name) +
             (container.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
        item->setData(0, Qt::UserRole + 3, "part-container");
        item->setIcon(0, resource_icon(container.feature_kind==zima::document::FeatureKind::Thread &&
            !container.thread.enabled ? QStringLiteral("cylinder") : feature_icon_name(container.feature_kind)));
        const auto owned_sketch = std::find_if(document.sketches.begin(),
            document.sketches.end(), [&](const auto& sketch) {
                return sketch.owner_container_id == container.id;
            });
        add_history_container_tree_children(item, container, construction_path,
            owned_sketch == document.sketches.end() ? nullptr : &*owned_sketch);
        const auto* part=workspace_.open_part(document.document_id);
        const auto boundary=part ? part->session.rollback_boundary(container.id) : std::nullopt;
        const auto issue=feature_reference_issue(container,document,references,
            boundary && boundary->input_body ? &boundary->input_body->mesh : nullptr);
        tree_reference_state_.apply(item,document.document_id,container.id,issue);
        for (int child=0;child<item->childCount();++child) {
            auto* row=item->child(child);
            if (row->data(0,Qt::UserRole+3).toString()=="part-container-entity")
                tree_reference_state_.apply(row,document.document_id,container.id,issue);
        }
        if (part && !part->session.calculated_boundaries().empty()) {
            const auto& errors = part->session.calculated_boundaries().back().calculation_errors;
            if (const auto failure = errors.find(container.id); failure != errors.end()) {
                item->setText(0, item->text(0) + tr(" [nevypočítáno]"));
                item->setToolTip(0, QString::fromStdString(failure->second));
                item->setForeground(0, QBrush(QColor(210, 75, 65)));
            }
        }
        if (container.suppressed) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
        }
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            if (index == part_rollback_->history_limit) {
                item->setForeground(0, QBrush(QColor(70, 190, 95)));
                QFont font = item->font(0);
                font.setBold(true);
                item->setFont(0, font);
            } else if (index > part_rollback_->history_limit) {
                item->setForeground(0, QBrush(QColor(125, 125, 125)));
            }
        }
    }
    for (const auto& sketch : document.sketches) {
        if (!sketch.owner_container_id.empty()) continue;
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(sketch.name) +
                (sketch.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch");
        tree_reference_state_.apply(item,document.document_id,sketch.id,sketch_reference_issue(sketch,document));
        item->setSelected(sketch.id == selected_sketch_id_);
        if (sketch.suppressed) {
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setForeground(0, QBrush(QColor(128, 128, 128)));
        }
        for (const auto& constraint : sketch.constraints) {
            auto* child = new QTreeWidgetItem(
                item, {sketch_constraint_label(constraint.kind)});
            child->setIcon(0, sketch_constraint_tree_icon(constraint.kind));
            child->setData(0, Qt::UserRole,
                QString::fromStdString(constraint.id));
            child->setData(0, Qt::UserRole + 3, "part-sketch-constraint");
            child->setData(0, Qt::UserRole + 4,
                QString::fromStdString(sketch.id));
        }
        for (const auto& dimension : sketch.dimensions) {
            auto* child = new QTreeWidgetItem(
                item, {sketch_dimension_label(dimension)});
            child->setData(
                0, Qt::UserRole, QString::fromStdString(dimension.id));
            child->setData(0, Qt::UserRole + 3, "part-sketch-dimension");
            child->setData(
                0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        }
        item->setExpanded(true);
    }
    for (const auto& object : document.constructions) {
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(object.name) +
                (object.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
        item->setData(0, Qt::UserRole + 1,
            QString::fromStdString(construction_path.encoded()));
        item->setData(0, Qt::UserRole + 3, "part-construction");
        if (object.suppressed) {
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setForeground(0, QBrush(QColor(128, 128, 128)));
        }
        item->setIcon(0, resource_icon(
            object.kind == zima::document::ConstructionKind::Point ? "point"
                : object.kind == zima::document::ConstructionKind::Curve3D
                    ? "sketch-3d"
                : object.kind == zima::document::ConstructionKind::Axis
                    ? "axis" : "plane"));
        add_construction_tree_children(item, object, construction_path);
        tree_reference_state_.apply(item,document.document_id,object.id,construction_reference_issue(object,references));
        for (int i=0;i<item->childCount();++i) {
            auto* row=item->child(i);
            if (row->data(0,Qt::UserRole+3).toString()=="curve3d-point") {
                if (const auto* point=document.find_construction(row->data(0,Qt::UserRole).toString().toStdString()))
                    tree_reference_state_.apply(row,document.document_id,point->id,construction_reference_issue(*point,references));
            }
        }
    }
    if (!document.history_order.empty()) {
        std::map<std::string, QTreeWidgetItem*> items_by_id;
        for (int index = parent->childCount() - 1; index >= 1; --index) {
            auto* child = parent->child(index);
            const auto role = child->data(0, Qt::UserRole + 3).toString();
            if (role != QStringLiteral("part-container") &&
                role != QStringLiteral("part-sketch") &&
                role != QStringLiteral("part-construction")) continue;
            child = parent->takeChild(index);
            items_by_id.emplace(
                child->data(0, Qt::UserRole).toString().toStdString(), child);
        }
        int insertion = 1;
        for (const auto& entry : document.history_order) {
            const auto found = items_by_id.find(entry.id);
            if (found == items_by_id.end()) continue;
            parent->insertChild(insertion++, found->second);
            items_by_id.erase(found);
        }
        for (const auto& [id, item] : items_by_id) {
            static_cast<void>(id);
            parent->insertChild(insertion++, item);
        }
    }
    std::size_t displayed_cursor = document.effective_history_cursor();
    if (!pending_creation_id.empty() && !document.history_order.empty()) {
        const auto pending = std::find_if(document.history_order.begin(),
            document.history_order.end(), [&](const auto& entry) {
                return entry.kind == zima::document::PartHistoryKind::Feature &&
                    entry.id == pending_creation_id;
            });
        if (pending != document.history_order.end() &&
            static_cast<std::size_t>(std::distance(
                document.history_order.begin(), pending)) < displayed_cursor) {
            --displayed_cursor;
        }
    }
    if (part_rollback_ &&
        part_rollback_->part_document_id == document.document_id &&
        part_rollback_->history_limit < document.history.size()) {
        const auto& edited_id =
            document.history[part_rollback_->history_limit].id;
        const auto edited = std::find_if(document.history_order.begin(),
            document.history_order.end(), [&](const auto& entry) {
                return entry.kind == zima::document::PartHistoryKind::Feature &&
                    entry.id == edited_id;
            });
        if (edited != document.history_order.end()) {
            displayed_cursor = static_cast<std::size_t>(
                std::distance(document.history_order.begin(), edited));
        }
    }
    const int cursor_position = 1 + static_cast<int>(displayed_cursor);
    if (document.body_history.bodies().empty()) {
        auto* body = new QTreeWidgetItem({tr("Těleso")});
        body->setIcon(0, resource_icon("result-body"));
        body->setData(0, Qt::UserRole, QString::fromStdString(document.document_id));
        body->setData(0, Qt::UserRole + 3, "part-result-body");
        parent->insertChild(std::min(cursor_position, parent->childCount()), body);
        if (part_history_insertion_allowed()) {
            auto* insert_here = new QTreeWidgetItem({tr("← Vložit zde")});
            insert_here->setData(0, Qt::UserRole + 3, "part-insert-here");
            auto font = insert_here->font(0);
            font.setBold(true);
            insert_here->setFont(0, font);
            insert_here->setForeground(0, QBrush(QColor("#4DD811")));
            parent->insertChild(std::min(cursor_position + 1, parent->childCount()), insert_here);
        }
    }
    add_pending_tree_item(parent, document.document_id, construction_path, false);
    if (!document.body_history.bodies().empty()) {
        std::map<std::string,QTreeWidgetItem*> entries;
        QTreeWidgetItem* pending_entry = nullptr;
        for (int index = parent->childCount() - 1; index >= 0; --index) {
            auto* row = parent->child(index);
            const auto id = row->data(0, Qt::UserRole).toString().toStdString();
            if (document.body_history.owner(id)) entries.emplace(id, parent->takeChild(index));
            else if (row->data(0, Qt::UserRole + 12).toBool()) pending_entry = parent->takeChild(index);
        }
        const auto& graph = document.body_history;
        const auto make_cursor = [](QTreeWidgetItem* owner, const char* kind, const std::string& id) {
            auto* row = new QTreeWidgetItem(owner, {QObject::tr("← Vložit zde")});
            row->setData(0, Qt::UserRole, QString::fromStdString(id));
            row->setData(0, Qt::UserRole + 3, kind);
            row->setForeground(0, QBrush(QColor("#4DD811"))); return row;
        };
        auto active_position = std::ranges::find(graph.order(),body_dialog_step_id_);
        if(active_position==graph.order().end())
            active_position=std::ranges::find(graph.order(),graph.active_body_id());
        const auto shade_downstream = [&](QTreeWidgetItem* item, std::size_t index) {
            if (active_position == graph.order().end() || index <= static_cast<std::size_t>(active_position - graph.order().begin())) return;
            const auto shade = [&](auto&& self, QTreeWidgetItem* child) -> void {
                child->setForeground(0, QBrush(QColor(125, 125, 125)));
                child->setToolTip(0, tr("Následuje za aktivním tělesem. Zpět do dílu obnoví celý výsledek."));
                for (int i = 0; i < child->childCount(); ++i) self(self, child->child(i));
            };
            shade(shade, item);
        };
        for (std::size_t index = 0; index <= graph.order().size(); ++index) {
            if (index == graph.insertion_cursor() && graph.active_body_id().empty() && part_history_insertion_allowed())
                make_cursor(parent, "part-body-insert-here", {});
            if (index == graph.order().size()) break;
            const auto& id = graph.order()[index];
            const auto* definition = graph.find(id);
            auto* row = new QTreeWidgetItem(parent, {QString::fromStdString(
                definition ? definition->name : graph.find_boolean(id)->name)});
            row->setIcon(0, resource_icon(definition&&definition->derived_copy ? (definition->derived_copy->pattern?"pattern":"mirror") : "result-body"));
            row->setData(0, Qt::UserRole, QString::fromStdString(id));
            row->setData(0, Qt::UserRole + 3, definition ? "part-body" : "part-body-boolean");
            if (id == graph.active_body_id() || id == body_dialog_step_id_) {
                row->setForeground(0, QBrush(QColor("#4DD811")));
                auto font = row->font(0); font.setBold(true); row->setFont(0, font);
            }
            if (!definition) { shade_downstream(row, index); continue; }
            auto* origin = add_origin_tree_item(row, id, false, construction_path);
            origin->setText(0, definition->derived_copy ? (definition->derived_copy->pattern?tr("Počátek Pole"):tr("Počátek Zrcadla")) : tr("Počátek tělesa"));
            if(definition->derived_copy) {
                const auto* source=graph.find(definition->derived_copy->source_id);
                auto* link=new QTreeWidgetItem(row,{tr("Zdroj: %1").arg(QString::fromStdString(source?source->name:definition->derived_copy->source_id))});
                link->setData(0,Qt::UserRole,QString::fromStdString(id));link->setData(0,Qt::UserRole+3,"mirror-source");
            }
            for (const auto& entry : definition->entries) {
                const auto found = entries.find(entry.id);
                if (found != entries.end()) { row->addChild(found->second); entries.erase(found); }
            }
            const auto& pending_body = sketch_properties_body_id_.empty()
                ? graph.active_body_id() : sketch_properties_body_id_;
            if (pending_entry && id == pending_body) {
                row->insertChild(std::min(static_cast<int>(definition->cursor)+1, row->childCount()), pending_entry);
                pending_entry = nullptr;
            }
            if (id == graph.active_body_id() && part_history_insertion_allowed()) {
                auto* cursor = make_cursor(row, "part-insert-here", id);
                row->removeChild(cursor);
                row->insertChild(static_cast<int>(definition->cursor)+1, cursor);
            }
            shade_downstream(row, index);
            row->setExpanded(true);
        }
        for (const auto& [id, row] : entries) parent->addChild(row);
        if (pending_entry) parent->addChild(pending_entry);
    }
}

void AssemblyWorkspaceWindow::add_assembly_tree_children(
    QTreeWidgetItem* parent,
    const std::string& assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    const auto* assembly = workspace_.open_assembly(assembly_document_id);
    if (assembly == nullptr) return;
    const auto references=assembly_reference_index(assembly->session.document());
    if (assembly_document_id == workspace_.active_document_id()) {
        add_origin_tree_item(parent, assembly_document_id, true, parent_path);
        for (const auto& sketch : assembly->session.document().sketches) {
            if (!sketch.owner_container_id.empty()) continue;
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(sketch.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
            item->setData(0, Qt::UserRole + 3, "assembly-sketch");
            tree_reference_state_.apply(item,assembly_document_id,sketch.id,sketch_reference_issue(sketch,assembly->session.document()));
            item->setSelected(sketch.id == selected_sketch_id_);
        }
        for (std::size_t cut_index = 0;
             cut_index < assembly->session.document().cuts.size(); ++cut_index) {
            const auto& cut = assembly->session.document().cuts[cut_index];
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(cut.definition.name) +
                    (cut.definition.suppressed
                        ? tr(" [potlačeno]") : QString{})});
            item->setData(0, Qt::UserRole,
                QString::fromStdString(cut.definition.id));
            item->setData(0, Qt::UserRole + 3, "assembly-cut");
            item->setIcon(0, resource_icon(
                feature_icon_name(cut.definition.feature_kind)));
            const auto owned_sketch = std::find_if(
                assembly->session.document().sketches.begin(),
                assembly->session.document().sketches.end(),
                [&](const auto& sketch) {
                    return sketch.owner_container_id == cut.definition.id;
                });
            add_history_container_tree_children(item, cut.definition, parent_path,
                owned_sketch == assembly->session.document().sketches.end()
                    ? nullptr : &*owned_sketch, true);
            tree_reference_state_.apply(item,assembly_document_id,cut.definition.id,
                feature_reference_issue(cut.definition,assembly->session.document(),references));
            item->setExpanded(true);
            if (cut.definition.suppressed || (assembly_cut_rollback_ &&
                    cut_index > assembly_cut_rollback_->cut_index)) {
                item->setForeground(0, QBrush(QColor(125, 125, 125)));
            } else if (assembly_cut_rollback_ &&
                       cut_index == assembly_cut_rollback_->cut_index) {
                item->setForeground(0, QBrush(QColor(70, 190, 95)));
                QFont font = item->font(0);
                font.setBold(true);
                item->setFont(0, font);
            }
        }
        for (const auto& object : assembly->session.document().constructions) {
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(object.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
            item->setData(0, Qt::UserRole + 1,
                QString::fromStdString(parent_path.encoded()));
            item->setData(0, Qt::UserRole + 3, "assembly-construction");
            item->setIcon(0, resource_icon(
                object.kind == zima::document::ConstructionKind::Point ? "point"
                    : object.kind == zima::document::ConstructionKind::Curve3D
                        ? "sketch-3d"
                    : object.kind == zima::document::ConstructionKind::Axis
                        ? "axis" : "plane"));
            add_construction_tree_children(item, object, parent_path);
            tree_reference_state_.apply(item,assembly_document_id,object.id,construction_reference_issue(object,references));
        }
    }
    add_snapshot_tree_children(
        parent, assembly->session.document().occurrence_snapshot(),
        assembly_document_id, parent_path, ancestor_suppressed);
    if (assembly_document_id == workspace_.active_document_id() &&
        parent_path.occurrence_ids.empty()) {
        auto* insert_here = new QTreeWidgetItem(parent, {tr("← Vložit zde")});
        insert_here->setData(0, Qt::UserRole + 3, "assembly-insert-here");
        auto font = insert_here->font(0);
        font.setBold(true);
        insert_here->setFont(0, font);
        insert_here->setForeground(0, QBrush(QColor("#4DD811")));
    }    if (assembly_document_id == workspace_.active_document_id())
        add_pending_tree_item(parent, assembly_document_id, parent_path, true);

}

void AssemblyWorkspaceWindow::add_snapshot_tree_children(
    QTreeWidgetItem* parent,
    const std::vector<zima::assembly::OccurrenceSnapshot>& snapshots,
    const std::string& owner_assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    const auto* owner=workspace_.open_assembly(owner_assembly_document_id);
    const auto references=owner ? assembly_reference_index(owner->session.document()) : zima::workspace::ReferenceIndex{};
    for (const auto& component : snapshots) {
        const bool suppressed = ancestor_suppressed ||
            component.manually_suppressed || component.dependency_suppressed;
        QString label = QString::fromStdString(component.name);
        if (component.manually_suppressed) label += tr(" [potlačeno]");
        else if (suppressed) {
            label += tr(" [potlačeno závislostí]");
        }
        else if (!component.visible) label += tr(" [skryto]");
        if (component.grounded && component.derived_source_id.empty()) label += tr(" [uzemněno]");
        auto* item = new QTreeWidgetItem(parent, {label});
        item->setIcon(0, resource_icon(component.source_kind == zima::assembly::ComponentSourceKind::Assembly
            ? "assembly" : "part"));
        const auto path = parent_path.child(component.occurrence_id);
        if(!component.derived_source_id.empty()) {
            item->setIcon(0,resource_icon(component.pattern_group?"pattern":"mirror"));
            auto* origin=add_origin_tree_item(item,component.occurrence_id,false,parent_path);
            origin->setText(0,component.pattern_group?tr("Počátek Pole"):tr("Počátek Zrcadla"));
        }
        item->setData(0, Qt::UserRole, QString::fromStdString(component.occurrence_id));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(path.encoded()));
        item->setData(0, Qt::UserRole + 2,
                      QString::fromStdString(component.source_document_id));
        item->setData(0, Qt::UserRole + 3,
            component.source_kind != zima::assembly::ComponentSourceKind::Part
                ? "assembly-occurrence" : "part-occurrence");
        item->setData(0, Qt::UserRole + 4,
                      QString::fromStdString(owner_assembly_document_id));
        if (owner) if (const auto* occurrence=owner->session.document().find_occurrence(component.occurrence_id))
            tree_reference_state_.apply(item,owner_assembly_document_id,component.occurrence_id,
                occurrence_reference_issue(*occurrence,references));
        const bool active_occurrence =
            path.encoded() == active_occurrence_path_ &&
            component.source_document_id == workspace_.active_document_id();
        if ((part_rollback_ && path.encoded() == part_rollback_->instance_path) ||
            (component.occurrence_id==primitive_parameter_owner_id_&&owner_assembly_document_id==workspace_.active_document_id()) || active_occurrence) {
            item->setForeground(0, QBrush(QColor(70, 190, 95)));
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        } else if (suppressed || !component.visible) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        }
        if (component.source_kind != zima::assembly::ComponentSourceKind::Part) {
            add_origin_tree_item(item, component.source_document_id, true, path);
            const auto* active_source = active_occurrence
                ? workspace_.open_assembly(component.source_document_id) : nullptr;
            if (active_source != nullptr) {
                for (const auto& object :
                     active_source->session.document().constructions) {
                    auto* construction_item = new QTreeWidgetItem(
                        item, {QString::fromStdString(object.name)});
                    construction_item->setData(0, Qt::UserRole,
                        QString::fromStdString(object.id));
                    construction_item->setData(0, Qt::UserRole + 1,
                        QString::fromStdString(path.encoded()));
                    construction_item->setData(
                        0, Qt::UserRole + 3, "assembly-construction");
                    tree_reference_state_.apply(construction_item,component.source_document_id,object.id,
                        construction_reference_issue(object,assembly_reference_index(active_source->session.document())));
                }
                add_snapshot_tree_children(
                    item, active_source->session.document().occurrence_snapshot(),
                    component.source_document_id, path,
                    suppressed || !component.visible);
            } else {
                add_snapshot_tree_children(
                    item, component.children, component.source_document_id, path,
                    suppressed || !component.visible);
            }
            item->setExpanded(true);
        } else if (active_occurrence) {
            const auto* active_part = workspace_.open_part(component.source_document_id);
            if (active_part != nullptr) {
                add_part_tree_children(item, active_part->session.document());
                item->setExpanded(true);
            }
        } else {
            add_origin_tree_item(item, component.source_document_id, false, path);
        }
    }
}

} // namespace zima::app
