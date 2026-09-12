#include <zima/workspace/placement_edit.hpp>
#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;

namespace {


// Same interaction as a numeric inline dimension: choose to commit, dismiss
// to cancel, then continue editing dimensions in View without Properties.
class InlineThreadSizeEdit final : public QComboBox {
public:
    using QComboBox::QComboBox;
    void open_after_release() {
        QTimer::singleShot(10,this,[this] {
            if (QApplication::mouseButtons() & Qt::LeftButton) {
                open_after_release();
                return;
            }
            show();
            setFocus(Qt::MouseFocusReason);
            showPopup();
        });
    }
    void hidePopup() override {
        QComboBox::hidePopup();
        hide();
        deleteLater();
    }
};

class InlineDimensionEdit final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            setProperty("cancelled", true);
            deleteLater();
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

} // namespace



QString AssemblyWorkspaceWindow::dimension_identifier(
    const std::string& owner, const std::string& key) const {
    const auto id = workspace_.active_document_id();
    if (const auto* part = workspace_.open_part(id))
        return QString::fromStdString(part->session.document().dimension_identifiers.identifier(owner,key));
    if (const auto* assembly = workspace_.open_assembly(id))
        return QString::fromStdString(assembly->session.document().dimension_identifiers.identifier(owner,key));
    return {};
}

void AssemblyWorkspaceWindow::edit_dimension_inline(
    const zima::viewer::ViewerCandidate& candidate) {
    if (candidate.semantic_key.starts_with("measurement:")) return;
    if(parameter_value_locked(candidate.owner_id,candidate.semantic_key).value_or(false)){state_->setText(tr("Hodnota je zamčená. Nejprve ji odemkněte."));return;}
    const auto value = viewer_->candidate_dimension_value(candidate);
    if (!value || candidate.kind != zima::viewer::CandidateKind::Dimension) return;
    if (candidate.semantic_key == "parameter:thread_designation") {
        if (!sketch_drag_dimension_id_.empty()) end_sketch_dimension_drag();
        auto* dialog = dynamic_cast<PrimitivePropertiesDialog*>(primitive_reference_dialog_);
        if (dialog == nullptr && properties_dialog_ == nullptr) {
            const auto document_id=workspace_.active_document_id();
            const auto* part=workspace_.open_part(document_id);
            const auto* opening=part == nullptr ? nullptr
                : part->session.document().find_container(candidate.owner_id);
            if (opening == nullptr || opening->feature_kind !=
                    zima::document::FeatureKind::Thread || !opening->thread.enabled) return;
            const auto standard=opening->thread.standard == zima::document::ThreadStandard::Metric
                ? "metric" : opening->thread.standard == zima::document::ThreadStandard::Whitworth
                    ? "whitworth" : "pipe";
            auto* selector=new InlineThreadSizeEdit(viewer_);
            selector->setObjectName("inlineThreadSizeEdit");
            const auto catalog=load_thread_catalog(standard);
            for (const auto& size : catalog) selector->addItem(size.designation);
            selector->setCurrentIndex(selector->findText(
                QString::fromStdString(opening->thread.designation)));
            const auto position=viewer_->candidate_dimension_label_position(candidate)
                .value_or(viewer_->last_pointer_position());
            selector->setFixedWidth(210);
            selector->move(std::clamp(position.x(),0,std::max(0,viewer_->width()-210)),
                std::clamp(position.y(),0,std::max(0,viewer_->height()-30)));
            connect(selector,&QComboBox::activated,this,
                [this,document_id,owner=candidate.owner_id,catalog,component=opening_component_edit_.second](int index) {
                    if (index<0 || static_cast<std::size_t>(index)>=catalog.size()) return;
                    auto* part=workspace_.open_part(document_id);
                    if (part == nullptr || workspace_.active_document_id()!=document_id) return;
                    auto next=part->session.document();
                    auto* opening=next.find_container(owner);
                    if (opening == nullptr) return;
                    const auto& size=catalog[index];
                    auto& thread=opening->thread;
                    thread.designation=size.designation.toStdString();
                    thread.nominal_diameter=size.nominal_diameter;
                    thread.pitch=size.pitch;
                    if (!thread.custom_profile_diameter)
                        thread.profile_diameter=size.internal_root_diameter;
                    if (thread.end_condition_forward == zima::document::EndCondition::Length &&
                        thread.length_end_condition == zima::document::EndCondition::Length)
                        thread.bore_length=std::max(thread.bore_length,
                            std::ceil((thread.length_forward+thread.runout_pitch_factor*thread.pitch)*1000.0)/1000.0);
                    try {
                        const auto& previous=part->session.calculated_boundaries();
                        auto calculated=calculate_part_with_resolved_references(next,&previous);
                        part->session.commit(std::move(next),std::move(calculated));
                        refresh_tabs();
                        show_parameter_dimensions(owner,component);
                    } catch (const std::exception& error) {
                        state_->setText(QString::fromUtf8(error.what()));
                    }
                });
            connect(selector,&QObject::destroyed,this,
                [this,document_id,owner=candidate.owner_id,component=opening_component_edit_.second] {
                    if (workspace_.active_document_id()==document_id)
                        show_parameter_dimensions(owner,component);
                });
            selector->open_after_release();
            return;
        }
        if (dialog != nullptr && dialog->owns_reference_owner(candidate.owner_id))
            static_cast<void>(dialog->open_thread_catalog());
        return;
    }
    const auto label_position =
        viewer_->candidate_dimension_label_position(candidate);
    // A double click is preceded by a press that may have started dimension
    // placement dragging. Finish it before creating the value editor;
    // otherwise the release following the double click refreshes the scene
    // underneath the editor and makes the dimension disappear.
    if (!sketch_drag_dimension_id_.empty()) end_sketch_dimension_drag();
    if (inline_dimension_edit_ != nullptr) inline_dimension_edit_->deleteLater();
    auto* edit = new InlineDimensionEdit(viewer_);
    inline_dimension_edit_ = edit;
    edit->setObjectName("inlineDimensionValueEdit");
    const auto identifier = dimension_identifier(candidate.owner_id, candidate.semantic_key);
    edit->setProperty("dimensionIdentifier", identifier);
    edit->setToolTip(identifier);
    edit->setText(QString::fromStdString(kernel::dimension_number(
        *value, viewer_->dimension_decimal_places())));
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedSize(104, 28);
    edit->setStyleSheet(
        "QLineEdit { background:#171A1D; color:#FFD400;"
        " border:1px solid #00DDF0; border-radius:3px;"
        " selection-background-color:#356E22; padding:2px 5px; }");
    const QPoint pointer = label_position.value_or(viewer_->last_pointer_position());
    edit->move(std::clamp(pointer.x() - edit->width() / 2, 0,
                             std::max(0, viewer_->width() - edit->width())),
               std::clamp(pointer.y() - edit->height() / 2, 0,
                             std::max(0, viewer_->height() - edit->height())));
    const QPointer<InlineDimensionEdit> guarded(edit);
    const auto commit = [this, candidate, guarded] {
        if (guarded.isNull() || guarded->property("committed").toBool() ||
            guarded->property("cancelled").toBool()) return;
        guarded->setProperty("committed", true);
        QString text = guarded->text().trimmed();
        text.replace(',', '.');
        bool valid{};
        const double parsed_value = text.toDouble(&valid);
        if (!valid || !std::isfinite(parsed_value)) {
            guarded->setProperty("committed", false);
            guarded->setStyleSheet(guarded->styleSheet() +
                " QLineEdit { border-color:#C64B4B; }");
            guarded->selectAll();
            return;
        }
        const double next_value = rounded_to_decimal_places(
            parsed_value, viewer_->dimension_decimal_places());
        try {
            if(parameter_value_locked(candidate.owner_id,candidate.semantic_key).value_or(false))throw std::runtime_error(tr("Hodnota je zamčená.").toStdString());
            if(candidate.semantic_key.starts_with("placement-reference:")){
                if(candidate.owner_id!=workspace_.active_document_id()||(properties_dialog_ && !component_placement_dialog_))
                    throw std::runtime_error("Kóta nepatří aktivní sestavě.");
                auto* source=workspace_.open_assembly(candidate.owner_id);
                if(!source)throw std::runtime_error("Sestava již není dostupná.");
                const auto separator=candidate.semantic_key.rfind(':');
                if(separator==std::string::npos||separator<=20)throw std::runtime_error("Neplatná reference kóty.");
                auto next=source->session.document();
                auto* occurrence=next.find_occurrence(candidate.semantic_key.substr(20,separator-20));
                const auto index=std::stoul(candidate.semantic_key.substr(separator+1));
                if (component_placement_dialog_) {
                    if (component_placement_assembly_document_id_ != candidate.owner_id ||
                        component_placement_dialog_->occurrence_id() != candidate.semantic_key.substr(20,separator-20))
                        throw std::runtime_error("Kóta nepatří upravované komponentě.");
                    auto references = component_placement_dialog_->pending_value().placement_references;
                    if (index >= references.size()) throw std::runtime_error("Reference kóty již neexistuje.");
                    auto& pending = references[index];
                    if (pending.offset_locked) throw std::runtime_error("Hodnota je zamčená.");
                    if ((pending.lower_limit && next_value < *pending.lower_limit) ||
                        (pending.upper_limit && next_value > *pending.upper_limit))
                        throw std::runtime_error("Hodnota je mimo povolené meze.");
                    pending.offset = next_value;
                    guarded->hide();
                    component_placement_dialog_->set_placement_references(std::move(references));
                    guarded->deleteLater();
                    return;
                }
                if(!occurrence||index>=occurrence->placement_references.size())
                    throw std::runtime_error("Reference kóty již neexistuje.");
                auto& row=occurrence->placement_references[index];
                if(row.offset_locked)throw std::runtime_error("Hodnota je zamčená.");
                if((row.lower_limit&&next_value<*row.lower_limit)||(row.upper_limit&&next_value>*row.upper_limit))
                    throw std::runtime_error("Hodnota je mimo povolené meze.");
                row.offset=next_value;
                next.calculate_placement_references();
                source->session.commit(std::move(next));
                guarded->hide();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();guarded->deleteLater();return;
            }
            const auto edit_sketch=[&](zima::sketcher::Sketch& sketch){
                if(candidate.semantic_key.starts_with("dimension:")){
                    if(!sketch.set_dimension_value(candidate.semantic_key.substr(10),next_value))
                        throw std::runtime_error("Dimension no longer exists");
                }else{
                    const auto radius=std::ranges::find(sketch.corner_radii,candidate.semantic_key.substr(17),&zima::sketcher::SketchCornerRadius::id);
                    if(radius==sketch.corner_radii.end())throw std::runtime_error("Corner radius no longer exists");
                    const auto first=radius->first_segment_id,second=radius->second_segment_id;
                    static_cast<void>(sketch.add_corner_fillet(first,second,next_value));
                }
                sketch.validate();
            };
            const bool sketch_dimension=candidate.semantic_key.starts_with("dimension:")||candidate.semantic_key.starts_with("corner_dimension:");
            if(sketch_dimension&&candidate.owner_id==active_sketch_id_){
                if(!mutate_active_sketch(edit_sketch))throw std::runtime_error("Sketch is no longer active");
                guarded->hide();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();guarded->deleteLater();return;
            }
            if(sketch_dimension&&properties_dialog_){
                bool edited=false;
                if (auto* dialog=dynamic_cast<SketchPropertiesDialog*>(properties_dialog_))
                    edited=dialog->mutate_sketch(candidate.owner_id, edit_sketch);
                if (auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(properties_dialog_)) {
                    if (property_owned_sketch_draft_ && property_owned_sketch_draft_->id==candidate.owner_id) {
                        auto next=*property_owned_sketch_draft_;
                        edit_sketch(next);
                        property_owned_sketch_draft_=std::move(next);
                        dialog->refresh_sketch_preview();
                        // Profile-frame caching intentionally ignores geometry;
                        // an edited Sketch must also refresh its static annotations.
                        preserve_view_on_refresh_=true;refresh_scene();
                        if(!pending_primitive_reference_index_)set_primitive_properties_dimension_selection();
                        edited=true;
                    }
                }
                if(auto* dialog=dynamic_cast<SectionPropertiesDialog*>(properties_dialog_)) {
                    auto sketch=dialog->values().sketch;
                    if(sketch.id==candidate.owner_id){edit_sketch(sketch);dialog->set_sketch(0,sketch);edited=true;}
                }
                if(!edited)if(auto* dialog=dynamic_cast<SweepPlacementDialog*>(properties_dialog_)){
                    auto pending=dialog->pending;
                    zima::document::visit_feature_sketches(pending,[&](auto& data,std::size_t stage){
                        auto sketch=zima::sketcher::Sketch::from_serialized(data);if(sketch.id!=candidate.owner_id)return;
                        edit_sketch(sketch);dialog->set_sketch(static_cast<unsigned>(stage),sketch);edited=true;
                    });
                }
                if(!edited&&construction_reference_dialog_&&construction_reference_dialog_->is_sweep()){
                    auto pending=construction_reference_dialog_->pending_sweep_value();
                    zima::document::visit_feature_sketches(pending,[&](const auto& data,std::size_t stage){
                        auto sketch=zima::sketcher::Sketch::from_serialized(data);if(sketch.id!=candidate.owner_id)return;
                        edit_sketch(sketch);construction_reference_dialog_->set_sweep_profile_sketch(stage,sketch);edited=true;
                    });
                }
                if(!edited)throw std::runtime_error(tr("Tuto kótu upravte tlačítkem Skica v otevřených vlastnostech.").toStdString());
                guarded->hide();guarded->deleteLater();return;
            }
            if (candidate.semantic_key.starts_with("parameter:") &&
                edge_treatment_dialog_ != nullptr &&
                candidate.owner_id == edge_treatment_preview_owner_id_) {
                guarded->hide();
                if (!edge_treatment_dialog_->set_inline_parameter_value(
                        candidate.semantic_key.substr(10), next_value)) {
                    throw std::runtime_error(
                        "This treatment dimension is not currently editable");
                }
                state_->setText(tr(
                    "Hodnota kóty byla přenesena do otevřeného okna vlastností."));
                guarded->deleteLater();
                return;
            }
            if (shaft_thread_dialog_ && candidate.semantic_key.starts_with("parameter:") &&
                shaft_thread_dialog_->pending().id==candidate.owner_id) {
                if (!shaft_thread_dialog_->set_numeric(std::string_view(candidate.semantic_key).substr(10),next_value))
                    throw std::runtime_error("Tuto kótu nelze upravit.");
                guarded->deleteLater();return;
            }
            if (candidate.semantic_key == "parameter:radius") {
                if (construction_reference_dialog_ != nullptr &&
                    construction_reference_dialog_->owns_reference_owner(candidate.owner_id)) {
                    if (!construction_reference_dialog_->set_curve_point_radius(
                            candidate.owner_id, next_value))
                        throw std::runtime_error("Tento radius nelze právě upravit.");
                    guarded->hide();
                    guarded->deleteLater();
                    return;
                }
                std::string radius_owner;
                const auto edit_path = [&](zima::document::ConstructionObject& path,
                                           const std::string& owner) {
                    if (path.kind != zima::document::ConstructionKind::Curve3D)
                        return false;
                    for (std::size_t i = 1; i + 1 < path.curve_points.size(); ++i) {
                        if (path.curve_points[i].id != candidate.owner_id) continue;
                        if (!path.curve_rounding_enabled ||
                            path.curve_type != zima::document::Curve3DType::Polyline ||
                            next_value < 0)
                            throw std::runtime_error("Tento radius nelze právě upravit.");
                        path.curve_points[i].curve_radius = next_value;
                        static_cast<void>(zima::document::curve3d_route(path));
                        radius_owner = owner;
                        return true;
                    }
                    return false;
                };
                if (auto* source = workspace_.open_part(workspace_.active_document_id())) {
                    auto next = source->session.document();
                    for (auto& path : next.constructions)
                        if (edit_path(path, path.id)) break;
                    if (radius_owner.empty()) {
                        for (auto& feature : next.history)
                            if (feature.feature_kind == zima::document::FeatureKind::Sweep3D &&
                                edit_path(feature.sweep3d.path, feature.id)) break;
                    }
                    if (!radius_owner.empty()) {
                        auto calculated = calculate_part_with_resolved_references(
                            next, &source->session.calculated_boundaries());
                        source->session.commit(std::move(next), std::move(calculated));
                    }
                } else if (auto* source = workspace_.open_assembly(workspace_.active_document_id())) {
                    auto next = source->session.document();
                    for (auto& path : next.constructions)
                        if (edit_path(path, path.id)) break;
                    if (!radius_owner.empty()) {
                        next.resolve_constructions();
                        source->session.commit(std::move(next));
                    }
                }
                if (!radius_owner.empty()) {
                    guarded->hide();
                    construction_dimension_object_id_ = radius_owner;
                    preserve_view_on_refresh_ = true;
                    refresh_tabs();
                    refresh_scene();
                    state_->setText(tr("Radius byl změněn přímo ve view."));
                    guarded->deleteLater();
                    return;
                }
            }
            PlacementReferenceDialog* inline_placement_dialog =
                construction_reference_dialog_ != nullptr
                ? static_cast<PlacementReferenceDialog*>(
                      construction_reference_dialog_)
                : primitive_reference_dialog_ != nullptr
                    ? primitive_reference_dialog_
                    : dynamic_cast<PlacementReferenceDialog*>(properties_dialog_);
            if (candidate.semantic_key.starts_with("parameter:") &&
                inline_placement_dialog != nullptr &&
                inline_placement_dialog->owns_parameter_owner(
                    candidate.owner_id)) {
                guarded->hide();
                if (!inline_placement_dialog->set_inline_parameter_value(
                        candidate.semantic_key.substr(10), next_value)) {
                    throw std::runtime_error(
                        "This dimension is not currently editable");
                }
                state_->setText(
                    tr("Hodnota kóty byla přenesena do otevřeného okna vlastností."));
                guarded->deleteLater();
                return;
            }
            if (candidate.semantic_key.starts_with("parameter:")) {
                const auto key = std::string_view(candidate.semantic_key).substr(10);
                const auto mutate_construction = [&](zima::document::ConstructionObject* construction,
                        const zima::kernel::ViewerReferenceGeometry& geometry) {
                    auto construction_key = key;
                    bool placement_key{};
                    constexpr std::string_view placement_prefix{"placement:"};
                    if (construction_key.starts_with(placement_prefix)) {
                        construction_key.remove_prefix(placement_prefix.size());
                        placement_key = true;
                    }
                    if (construction->kind ==
                            zima::document::ConstructionKind::Point ||
                        placement_key) {
                        if (!workspace::assign_placement_dimension(*construction, geometry,
                                construction_key, next_value)) return false;
                    } else if (construction->kind ==
                                   zima::document::ConstructionKind::Axis &&
                               key == "length") {
                        if (next_value <= 1.0e-9)
                            throw std::runtime_error(
                                "Dimension must be positive");
                        construction->display_size = next_value;
                    } else if (construction->kind ==
                                   zima::document::ConstructionKind::Plane &&
                               key == "offset") {
                        construction->offset = next_value;
                    } else {
                        return false;
                    }
                    return true;
                };
                bool construction_changed{};
                const zima::document::ConstructionObject* existing = nullptr;
                if (const auto* source = workspace_.open_part(workspace_.active_document_id()))
                    existing = source->session.document().find_construction(candidate.owner_id);
                else if (const auto* source = workspace_.open_assembly(workspace_.active_document_id()))
                    existing = source->session.document().find_construction(candidate.owner_id);
                if (existing) {
                    auto pending = *existing;
                    construction_changed = mutate_construction(&pending,
                        workspace::placement_edit_geometry(workspace_, workspace_.active_document_id(), candidate.owner_id));
                    if (construction_changed)
                        static_cast<void>(workspace::commit_construction(workspace_,
                            workspace_.active_document_id(), std::move(pending), workspace::ConstructionEditMode::Replace));
                }
                if (construction_changed) {
                    guarded->hide();
                    construction_dimension_object_id_ = candidate.owner_id;
                    preserve_view_on_refresh_ = true;
                    refresh_tabs();
                    refresh_scene();
                    state_->setText(
                        tr("Hodnota kóty byla změněna přímo ve view."));
                    guarded->deleteLater();
                    return;
                }
            }
            auto* part = workspace_.open_part(workspace_.active_document_id());
            if (part == nullptr) throw std::runtime_error(
                "Inline dimension editing currently requires an active Part");
            auto next = part->session.document();
            bool changed{};
            if (const auto* body=next.body_history.find(candidate.owner_id);
                    body && candidate.semantic_key.starts_with("parameter:placement:")) {
                auto updated=*body;
                auto& placement=updated.scope.placement;
                const auto geometry=workspace::placement_edit_geometry(workspace_,workspace_.active_document_id(),candidate.owner_id);
                const auto key=std::string_view(candidate.semantic_key).substr(std::string_view("parameter:placement:").size());
                changed = workspace::assign_placement_dimension(placement, geometry, key, next_value);
                if(changed)next.body_history.update_body(std::move(updated));
            } else if (candidate.semantic_key.starts_with("dimension:")) {
                const auto sketch = std::find_if(next.sketches.begin(),
                    next.sketches.end(), [&](const auto& value) {
                        return value.id == candidate.owner_id;
                    });
                changed = sketch != next.sketches.end() &&
                    sketch->set_dimension_value(
                        candidate.semantic_key.substr(10), next_value);
            } else if (candidate.semantic_key.starts_with("corner_dimension:")) {
                if (next_value <= 1.0e-9)
                    throw std::runtime_error("Corner radius must be positive");
                const auto sketch = std::find_if(next.sketches.begin(),
                    next.sketches.end(), [&](const auto& value) {
                        return value.id == candidate.owner_id;
                    });
                if (sketch != next.sketches.end()) {
                    const auto radius_id = candidate.semantic_key.substr(17);
                    const auto radius = std::ranges::find_if(
                        sketch->corner_radii, [&](const auto& value) {
                            return value.id == radius_id;
                        });
                    if (radius != sketch->corner_radii.end()) {
                        const auto first_segment_id = radius->first_segment_id;
                        const auto second_segment_id = radius->second_segment_id;
                        static_cast<void>(sketch->add_corner_fillet(
                            first_segment_id, second_segment_id, next_value));
                        changed = true;
                    }
                }
            } else if (candidate.semantic_key.starts_with("parameter:")) {
                const auto container = std::find_if(next.history.begin(),
                    next.history.end(), [&](const auto& value) {
                        return value.id == candidate.owner_id;
                    });
                if (container == next.history.end()) {
                    throw std::runtime_error("Dimension owner no longer exists");
                }
                const auto key = candidate.semantic_key.substr(10);
                using zima::document::FeatureKind;
                const auto positive = [&](double& target, bool allow_zero = false) {
                    if (next_value < (allow_zero ? 0.0 : 1.0e-9))
                        throw std::runtime_error("Dimension must be positive");
                    target = next_value;
                    changed = true;
                };
                if (key.starts_with("placement:")) {
                    const auto placement_key = std::string_view(key).substr(
                        std::string_view{"placement:"}.size());
                    // Use the same persisted reference universe as the displayed
                    // dimensions, including Body Origins in the owner's frame.
                    const auto geometry = workspace::placement_edit_geometry(
                        workspace_, workspace_.active_document_id(), candidate.owner_id);
                    changed = workspace::assign_placement_dimension(
                        container->placement, geometry, placement_key, next_value);
                } else if (container->feature_kind == FeatureKind::Sketch &&
                           key == "profile_offset") {
                    const auto sketch = std::find_if(next.sketches.begin(),
                        next.sketches.end(), [&](const auto& value) {
                            return value.owner_container_id == container->id;
                        });
                    if (sketch != next.sketches.end()) {
                        sketch->plane_offset = next_value; changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Box) {
                    if (key == "length") positive(container->box.length);
                    else if (key == "width") positive(container->box.width);
                    else if (key == "height") positive(container->box.height);
                } else if (container->feature_kind == FeatureKind::ShaftThread) {
                    if (key=="root_diameter") positive(container->shaft_thread.root_diameter);
                    else if (key=="length" && container->shaft_thread.end_condition==zima::document::EndCondition::Length)
                        positive(container->shaft_thread.length);
                } else if (container->feature_kind == FeatureKind::Thread) {
                    auto& thread = container->thread;
                    if (key == "bore_length" && thread.end_condition_forward == zima::document::EndCondition::Length)
                        positive(thread.bore_length);
                    else if (key == "bore_diameter" && !thread.enabled)
                        positive(thread.nominal_diameter);
                    else if (key == "thread_length" && thread.enabled &&
                             thread.length_end_condition == zima::document::EndCondition::Length) {
                        positive(thread.length_forward);
                        if (thread.end_condition_forward == zima::document::EndCondition::Length)
                            thread.bore_length = std::max(thread.bore_length,
                                std::ceil((thread.length_forward + thread.runout_pitch_factor * thread.pitch) * 1000.0) / 1000.0);
                    } else if (key == "chamfer_depth" && thread.chamfer_enabled)
                        positive(thread.chamfer_depth);
                    else if ((key == "chamfer_angle" && thread.chamfer_enabled) ||
                             (key == "drill_point_angle" && container->hole.drill_point_enabled)) {
                        if (next_value <= 0 || next_value >= 180)
                            throw std::runtime_error("Opening angle must be in (0, 180) degrees");
                        if (key == "chamfer_angle") thread.chamfer_angle_degrees = next_value;
                        else container->hole.drill_point_angle_degrees = next_value;
                        changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Cylinder) {
                    if (key == "radius") positive(container->cylinder.radius);
                    else if (key == "height") positive(container->cylinder.height);
                } else if (container->feature_kind == FeatureKind::Sphere &&
                           key == "radius") positive(container->sphere.radius);
                else if (container->feature_kind == FeatureKind::Cone) {
                    if (key == "bottom_radius") positive(container->cone.bottom_radius);
                    else if (key == "top_radius") positive(container->cone.top_radius, true);
                    else if (key == "height") positive(container->cone.height);
                } else if (container->feature_kind == FeatureKind::Pyramid) {
                    if (key == "length") positive(container->pyramid.length);
                    else if (key == "width") positive(container->pyramid.width);
                    else if (key == "height") positive(container->pyramid.height);
                } else if (container->feature_kind == FeatureKind::Wedge) {
                    if (key == "length") positive(container->wedge.length);
                    else if (key == "width") positive(container->wedge.width);
                    else if (key == "height") positive(container->wedge.height);
                    else if (key == "top_offset") {
                        container->wedge.top_offset = next_value; changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Extrusion) {
                    if (key == "length_forward") positive(
                        container->extrusion.length_forward);
                    else if (key == "length_reverse") positive(
                        container->extrusion.length_reverse);
                    else if (key == "profile_offset") {
                        container->extrusion.profile_plane_offset = next_value;
                        changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Revolution) {
                    if (key == "angle" || key == "length_reverse") {
                        if (next_value <= 0.0 || next_value > 360.0)
                            throw std::runtime_error(
                                "Revolution angle must be in (0, 360]");
                        if (key == "length_reverse")
                            container->revolution.angle_reverse = next_value;
                        else container->revolution.angle_degrees = next_value;
                        changed = true;
                    } else if (key == "profile_offset") {
                        container->revolution.profile_plane_offset = next_value;
                        changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Fillet ||
                           container->feature_kind == FeatureKind::Chamfer) {
                    if (key == "size" || key == "primary") {
                        positive(container->edge_treatment.primary_size);
                    } else if (key == "secondary") {
                        positive(container->edge_treatment.secondary_size);
                    } else if (key == "treatment_angle") {
                        if (next_value <= 0.0 || next_value >= 90.0) {
                            throw std::runtime_error(
                                "Chamfer angle must be in (0, 90) degrees");
                        }
                        container->edge_treatment.angle_degrees = next_value;
                        changed = true;
                    }
                } else if (container->feature_kind == FeatureKind::Shell &&
                           key == "thickness") {
                    positive(container->shell.thickness);
                }
            }
            if(!changed&&sketch_dimension)for(auto& feature:next.history){
                zima::document::visit_feature_sketches(feature,[&](auto& data,std::size_t){
                    auto sketch=zima::sketcher::Sketch::from_serialized(data);if(sketch.id!=candidate.owner_id)return;
                    edit_sketch(sketch);data=sketch.serialized();changed=true;
                    construction_dimension_object_id_=feature.id;
                });
            }
            if (!changed) throw std::runtime_error(
                "This dimension is not directly editable");
            workspace::commit_part_parameter_edit(*part, kernel_, std::move(next),
                candidate.owner_id, candidate.semantic_key.starts_with("parameter:"),
                part_calculation_policy());
            // Remove the editor before rebuilding the scene. Keeping the
            // child QLineEdit over the old label while refresh_scene() swaps
            // the dimension mesh makes a successful edit look as if the
            // dimension disappeared and its input window remained open.
            guarded->hide();
            if(!sketch_dimension||construction_dimension_object_id_.empty())construction_dimension_object_id_ = candidate.owner_id;
            preserve_view_on_refresh_ = true;
            refresh_tabs();
            refresh_scene();
            state_->setText(tr("Hodnota kóty byla změněna přímo ve view."));
            guarded->deleteLater();
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
            // The attempted edit is transactional. Explicitly restore the
            // unchanged scene and close the editor after a rejected solve or
            // calculation. A submitted value must never leave a stale input
            // floating over the restored dimension.
            guarded->hide();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            guarded->deleteLater();
        }
    };
    connect(edit, &QLineEdit::returnPressed, this, commit);
    connect(edit, &QLineEdit::editingFinished, this, commit);
    connect(edit, &QObject::destroyed, this, [this, edit] {
        if (inline_dimension_edit_ == edit) inline_dimension_edit_ = nullptr;
    });
    edit->show();
    edit->raise();
    edit->setFocus(Qt::MouseFocusReason);
    edit->selectAll();
}

} // namespace zima::app
