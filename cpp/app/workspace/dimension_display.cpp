#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::commit_dimension_layout(const zima::kernel::EdgeReference& reference,zima::kernel::DimensionLayout layout) {
    if(reference.instance_path!=active_occurrence_path_||(!part_element_context_menu_enabled(reference.owner_id)&&reference.owner_id!=active_sketch_id_))throw std::invalid_argument("Dimension is outside the active editing occurrence");
    if(const auto* sketch=active_sketch();sketch && reference.owner_id==sketch->id) {
        if(mutate_active_sketch([&](auto& pending){zima::kernel::store_dimension_layout(pending.dimension_layouts,reference,layout);})) {
            preserve_view_on_refresh_=true;refresh_scene();refresh_tabs();
            viewer_->confirm_reference(reference.owner_id,reference.semantic_key,reference.instance_path,zima::viewer::CandidateKind::Dimension);
        }
        return;
    }
    const auto id=workspace_.active_document_id();
    if(auto* part=workspace_.open_part(id)) {
        auto next=part->session.document();zima::kernel::store_dimension_layout(next.dimension_layouts,reference,layout);
        part->session.commit(std::move(next),part->session.calculated_boundaries());
    } else if(auto* assembly=workspace_.open_assembly(id)) {
        auto next=assembly->session.document();zima::kernel::store_dimension_layout(next.dimension_layouts,reference,layout);
        assembly->session.commit(std::move(next));
    }
    preserve_view_on_refresh_=true;refresh_scene();refresh_tabs();
}
void AssemblyWorkspaceWindow::show_dimension_layout_properties(const zima::viewer::ViewerCandidate& candidate) {
    if(properties_dialog_)return;
    const auto source=viewer_->dimension_source(candidate);if(!source)return;
    if(source->reference.instance_path!=active_occurrence_path_)return;
    if(candidate.semantic_key.starts_with("dimension:")) {
        const auto dimension_id=candidate.semantic_key.substr(10);
        const std::vector<zima::sketcher::Sketch>* sketches=nullptr;
        if(const auto* part=workspace_.open_part(workspace_.active_document_id()))sketches=&part->session.document().sketches;
        else if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id()))sketches=&assembly->session.document().sketches;
        if(sketches)for(const auto& sketch:*sketches)if(std::ranges::any_of(sketch.dimensions,[&](const auto& d){return d.id==dimension_id;})){
            show_sketch_dimension_properties(sketch.id,dimension_id);return;
        }
    }
    zima::kernel::DimensionLayout initial{0,8.0,0,0};
    const auto id=workspace_.active_document_id();
    const std::vector<zima::kernel::DimensionLayoutEntry>* entries=nullptr;
    if(const auto* part=workspace_.open_part(id))entries=&part->session.document().dimension_layouts;
    else if(const auto* assembly=workspace_.open_assembly(id))entries=&assembly->session.document().dimension_layouts;
    if(entries)if(const auto* value=zima::kernel::find_dimension_layout(*entries,source->reference))initial=*value;
    auto* dialog=new DimensionPropertiesDialog(*source,initial,[this,id,reference=source->reference](auto layout){if(workspace_.active_document_id()!=id)throw std::runtime_error("Active dimension document changed");commit_dimension_layout(reference,layout);},this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);properties_dialog_=dialog;viewer_->set_dimension_layout_editable(false);
    connect(dialog,&QObject::destroyed,this,[this,dialog]{if(properties_dialog_==dialog)properties_dialog_=nullptr;viewer_->set_dimension_layout_editable(!sketch_universal_dimension_active_&&!sweep_profile_sketch_draft_);});
    dialog->show();
}

void AssemblyWorkspaceWindow::update_assembly_dimension_visibility() {
    if(!viewer_)return;
    const auto wanted=properties_dialog_&&!properties_dialog_instance_path_.empty()?properties_dialog_instance_path_:assembly_dimension_path_;
    const auto owner=workspace_.active_document_id();
    viewer_->set_dimension_visibility_filter([wanted,owner](const auto& dimension){
        const auto& key=dimension.reference.semantic_key;
        if(!key.starts_with("placement-reference:"))return true;
        if(wanted.empty()||dimension.reference.owner_id!=owner)return false;
        const auto end=key.rfind(':');if(end<=20)return false;
        auto path=dimension.reference.instance_path.empty()?zima::assembly::InstancePath{}:zima::assembly::InstancePath::decode(dimension.reference.instance_path);
        return path.child(key.substr(20,end-20)).encoded()==wanted;
    });
}
bool AssemblyWorkspaceWindow::finish_parameter_dimensions() {
    if (properties_dialog_ || !active_sketch_id_.empty() ||
        (construction_dimension_object_id_.empty()&&assembly_dimension_path_.empty())) return false;
    construction_dimension_object_id_.clear();
    assembly_dimension_path_.clear();update_assembly_dimension_visibility();
    opening_component_edit_ = {};
    viewer_->clear_selection();
    preserve_view_on_refresh_ = true;
    refresh_scene();
    return true;
}

void AssemblyWorkspaceWindow::show_parameter_dimensions(
    const std::string& owner_id, const std::string& component) {
    if (owner_id.empty()) return;
    construction_dimension_object_id_ = owner_id;
    opening_component_edit_={owner_id,component};
    preserve_view_on_refresh_ = true;
    refresh_scene();
    if (!component.empty()) {
        for (QTreeWidgetItemIterator i(tree_);*i;++i) {
            if ((*i)->data(0,Qt::UserRole+3).toString()=="part-opening-component" &&
                (*i)->data(0,Qt::UserRole).toString().toStdString()==owner_id &&
                (*i)->data(0,Qt::UserRole+5).toString().toStdString()==component) {
                tree_->setCurrentItem(*i);
                break;
            }
        }
    }
    state_->setText(tr(
        "Parametrické kóty jsou zobrazené. Dvojklikem na kótu upravíte hodnotu."));
}

bool AssemblyWorkspaceWindow::is_edge_treatment_feature(
    const std::string& owner_id) const {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* container = part == nullptr
        ? nullptr : part->session.document().find_container(owner_id);
    return container != nullptr &&
        (container->feature_kind == zima::document::FeatureKind::Fillet ||
         container->feature_kind == zima::document::FeatureKind::Chamfer);
}

std::set<std::size_t>
AssemblyWorkspaceWindow::edge_treatment_feature_edges(
    const std::string& owner_id,
    std::string instance_path) const {
    if (!is_edge_treatment_feature(owner_id) || viewer_ == nullptr) return {};
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr ||
        part->session.document().find_container(owner_id) == nullptr) return {};
    if (instance_path.empty()) {
        instance_path = resolve_active_occurrence(
            part->session.document().document_id).value_or(std::string{});
    }
    return viewer_->edge_treatment_boundary_edge_indices(
        owner_id, instance_path);
}

} // namespace zima::app
