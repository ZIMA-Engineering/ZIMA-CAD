#include "assembly_workspace_window.hpp"
#include "fem_body_dialog.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/fem_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <limits>
#ifdef ZIMA_ENABLE_FEM_MESHER
#include "fem_linear_statics.hpp"
#include "fem_volume_mesh.hpp"
#endif

namespace zima::app {
namespace {
#ifdef ZIMA_ENABLE_FEM_MESHER
FemElasticMaterial elastic_material(const document::MaterialData& data) {
    const auto number=[&](const char* key) {
        const auto value=data.properties.find(key);
        if(value==data.properties.end())return std::numeric_limits<double>::quiet_NaN();
        bool valid=false;
        const double result=QString::fromStdString(value->second).toDouble(&valid);
        return valid?result:std::numeric_limits<double>::quiet_NaN();
    };
    double modulus=number("YOUNG_MODULUS");
    const auto unit=data.units.find("YOUNG_MODULUS");
    if(unit!=data.units.end()){
        static const std::map<std::string,double> factors{{"Pa",1e-6},{"kPa",1e-3},{"MPa",1},
            {"GPa",1000},{"psi",.006894757293168}};
        const auto factor=factors.find(unit->second);
        modulus=factor==factors.end()?std::numeric_limits<double>::quiet_NaN():modulus*factor->second;
    }
    return {modulus,number("POISSON_RATIO")};
}
#endif
class FemMeshSettingsDialog final : public ui::PropertiesSubWindow {
public:
    FemMeshSettingsDialog(document::FemMeshControls initial,
            std::function<void(document::FemMeshControls)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Mesh settings"),parent),initial_(initial),commit_(std::move(commit)) {
        setObjectName("femMeshSettingsDialog");
        setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;
        const auto length=[this,form](const QString& label,const char* name,double value) {
            auto* field=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
            field->setObjectName(name);
            field->setRange(0,std::numeric_limits<double>::max());
            field->setValue(value);
            form->addRow(label,field);
            return field;
        };
        minimum_=length(tr("Minimum element size"),"femMeshMinimum",initial_.minimum_size_mm);
        maximum_=length(tr("Maximum element size"),"femMeshMaximum",initial_.maximum_size_mm);
        layers_=new QDoubleSpinBox(this);
        layers_->setObjectName("femMeshLayers");
        layers_->setDecimals(0);
        layers_->setRange(1,std::numeric_limits<unsigned>::max());
        layers_->setValue(initial_.elements_through_thickness);
        form->addRow(tr("Elements through thickness"),layers_);
        order_=new QComboBox(this);
        order_->setObjectName("femMeshOrder");
        order_->addItem(tr("Linear"),static_cast<int>(document::FemElementOrder::Linear));
        order_->addItem(tr("Quadratic"),static_cast<int>(document::FemElementOrder::Quadratic));
        order_->setCurrentIndex(order_->findData(static_cast<int>(initial_.order)));
        form->addRow(tr("Element order"),order_);
        content_layout()->addLayout(form);
        status_=new QLabel(this);
        status_->setWordWrap(true);
        content_layout()->addWidget(status_);
        set_initial_size({470,245});
    }
protected:
    bool submit() override {
        auto value=initial_;
        value.minimum_size_mm=minimum_->value();
        value.maximum_size_mm=maximum_->value();
        value.elements_through_thickness=static_cast<unsigned>(layers_->value());
        value.order=static_cast<document::FemElementOrder>(order_->currentData().toInt());
        try {
            commit_(value);
            return true;
        }catch(const std::exception& error) {
            status_->setText(tr(error.what()));
            return false;
        }
    }
private:
    document::FemMeshControls initial_;
    std::function<void(document::FemMeshControls)> commit_;
    QDoubleSpinBox *minimum_{},*maximum_{},*layers_{};
    QComboBox* order_{};
    QLabel* status_{};
};
}

bool AssemblyWorkspaceWindow::active_fem_body()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->fem.has_value();
}

#ifdef ZIMA_ENABLE_FEM_MESHER
struct AssemblyWorkspaceWindow::GeneratedFemMesh {
    std::string document_id,body_id;
    kernel::BodySnapshot source;
    document::FemDefinition definition;
    FemVolumeMesh mesh;
    struct Solution {
        document::FemDefinition definition;
        FemElasticMaterial material;
        kernel::ViewerMesh deformation;
        double maximum_mm{};
    };
    std::optional<Solution> solution;
};
#endif

const kernel::ViewerMesh* AssemblyWorkspaceWindow::active_fem_deformation() const {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(!active_generated_fem_mesh()||!generated_fem_mesh_->solution)return nullptr;
    const auto& calculated=*generated_fem_mesh_->solution;
    const auto& id=workspace_.active_document_id();
    const auto& graph=workspace_.open_part(id)->session.document().body_history;
    const auto& definition=*graph.find(graph.active_body_id())->fem;
    if(calculated.definition==definition&&calculated.material==elastic_material(workspace::material_data(workspace_,id)))
        return &calculated.deformation;
#endif
    return nullptr;
}

void AssemblyWorkspaceWindow::solve_fem_statics() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(properties_dialog_||!active_fem_body())return;
    try {
        if(!active_generated_fem_mesh())throw std::runtime_error("Generate the FEM mesh before solving.");
        const auto mesh=generated_fem_mesh_;
        const auto& id=workspace_.active_document_id();
        const auto& graph=workspace_.open_part(id)->session.document().body_history;
        const auto definition=*graph.find(graph.active_body_id())->fem;
        const auto material=elastic_material(workspace::material_data(workspace_,id));
        if(!active_fem_deformation()){
            auto solution=ui::run_background_task([mesh,definition,material]{
                const auto result=solve_fem_linear_statics(mesh->mesh,definition,material);
                return GeneratedFemMesh::Solution{definition,material,fem_deformed_mesh(mesh->mesh,result),
                    result.maximum_displacement_mm};
            });
            mesh->solution=std::move(solution);
        }
        if(!active_fem_deformation())return;
        show_fem_deformation_=true;
        preserve_view_on_refresh_=true;
        refresh_scene();
        rebuild_application_toolbar();
        state_->setText(tr("Maximum displacement: %1 mm").arg(mesh->solution->maximum_mm,0,'g',6));
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
#endif
}

const kernel::ViewerMesh* AssemblyWorkspaceWindow::active_generated_fem_mesh() const {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(!generated_fem_mesh_||generated_fem_mesh_->document_id!=workspace_.active_document_id())return nullptr;
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    if(!part)return nullptr;
    const auto& graph=part->session.document().body_history;
    if(graph.active_body_id()!=generated_fem_mesh_->body_id)return nullptr;
    const auto* body=graph.find(graph.active_body_id());
    if(!body||!body->fem||body->fem->mesh!=generated_fem_mesh_->definition.mesh||
            body->fem->source_id!=generated_fem_mesh_->definition.source_id)return nullptr;
    const auto same_region=[](const auto& a,const auto& b) {
        return a.id==b.id&&a.region==b.region&&a.references==b.references&&a.projected_sketch==b.projected_sketch&&
            a.placement.reference_valid==b.placement.reference_valid&&
            (!a.projected_sketch||
                std::tie(a.placement.x,a.placement.y,a.placement.z,a.placement.rotation_x,a.placement.rotation_y,a.placement.rotation_z)==
                std::tie(b.placement.x,b.placement.y,b.placement.z,b.placement.rotation_x,b.placement.rotation_y,b.placement.rotation_z))&&
            (!a.references.empty()||a.region!=document::FemRegionKind::Point||
                std::tie(a.placement.x,a.placement.y,a.placement.z)==
                std::tie(b.placement.x,b.placement.y,b.placement.z));
    };
    if(!std::ranges::equal(body->fem->conditions,generated_fem_mesh_->definition.conditions,same_region))return nullptr;
    try {
        if(workspace::read_fem_source(part->session,body->scope.id).shares_with(generated_fem_mesh_->source))
            return &generated_fem_mesh_->mesh.surface;
    }catch(const std::invalid_argument&) {}
#endif
    return nullptr;
}

void AssemblyWorkspaceWindow::generate_fem_mesh() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(properties_dialog_||!active_fem_body())return;
    const auto document_id=workspace_.active_document_id();
    const auto& session=workspace_.open_part(document_id)->session;
    const auto& graph=session.document().body_history;
    const auto body_id=graph.active_body_id();
    try {
        if(!active_generated_fem_mesh()) {
            const auto source=workspace::read_fem_source(session,body_id);
            const auto definition=*graph.find(body_id)->fem;
            auto mesh=ui::run_background_task([source,definition]{return generate_fem_volume_mesh(source.get(),definition);});
            generated_fem_mesh_=std::make_shared<GeneratedFemMesh>(GeneratedFemMesh{
                document_id,body_id,source,definition,std::move(mesh)});
            if(!active_generated_fem_mesh())return;
        }
        show_generated_fem_mesh_=true;
        show_fem_deformation_=false;
        preserve_view_on_refresh_=true;
        refresh_scene();
        rebuild_application_toolbar();
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
#endif
}

void AssemblyWorkspaceWindow::show_fem_mesh_settings() {
    if(properties_dialog_||!active_fem_body())return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto& document=part->session.document();
    auto initial=*document.body_history.find(document.body_history.active_body_id());
    const auto edit=workspace::prepare_fem_edit(document,initial.scope.id);
    const auto controls=initial.fem->mesh;
    auto* dialog=new FemMeshSettingsDialog(controls,[this,edit,initial=std::move(initial)](auto value) {
        auto pending=initial;
        pending.fem->mesh=value;
        static_cast<void>(workspace::commit_fem(workspace_,edit,std::move(pending)));
    },this);
    properties_dialog_=dialog;
    properties_dialog_instance_path_=workspace_.active_occurrence_path();
    connect(dialog,&QDialog::finished,this,[this] {
        properties_dialog_=nullptr;
        properties_dialog_instance_path_.clear();
        refresh_tabs();
        rebuild_application_toolbar();
    });
    rebuild_application_toolbar();
    dialog->show();
}

void AssemblyWorkspaceWindow::show_fem_body_properties(const std::string& id) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);
    if(!part)return;
    const auto& graph=part->session.document().body_history;
    if(id.empty()&&!graph.active_body_id().empty())return;
    std::shared_ptr<workspace::BodyGraphEdit> edit;
    std::vector<std::string> candidates;
    try {
        edit=std::make_shared<workspace::BodyGraphEdit>(workspace::prepare_fem_edit(part->session.document(),id));
        candidates=workspace::fem_source_candidates(part->session,*edit);
    }catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    if(id.empty()&&candidates.empty()){state_->setText(tr("FEM requires a valid calculated solid. Regenerate the source first."));return;}
    struct Source {QString name;kernel::BodySnapshot geometry;};std::map<std::string,Source> sources;
    for(const auto& source:candidates) {
        const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
        sources.emplace(source,Source{QString::fromStdString(body?body->name:operation->name),part->session.calculated_boundaries().back().body_outputs.at(source)});
    }
    document::BodyHistory initial;
    if(!id.empty())initial=*graph.find(id);
    else {
        initial.scope.id=edit->object_id;initial.fem=document::FemDefinition{};
        const auto prefix=tr("FEM Body").toStdString();
        for(unsigned number=1;;++number) {
            initial.name=prefix+" "+std::to_string(number);
            if(std::ranges::none_of(graph.bodies(),[&](const auto& body){return body.name==initial.name;}))break;
        }
        if(tree_->currentItem()) {
            const auto selected=tree_->currentItem()->data(0,Qt::UserRole).toString().toStdString();
            if(sources.contains(selected))initial.fem->source_id=selected;
        }
        if(initial.fem->source_id.empty()&&candidates.size()==1)initial.fem->source_id=candidates.front();
    }
    const auto boundary=id.empty()?graph.insertion_cursor():static_cast<std::size_t>(std::ranges::find(graph.order(),id)-graph.order().begin());
    std::vector<std::string> context;
    for(const auto& source:graph.available_before(boundary))if(sources.contains(source)) {
        const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
        if(body?body->visible:operation->visible)context.push_back(source);
    }
    const auto prefix=workspace_.active_occurrence_path();
    auto* dialog=new FemBodyDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_fem(workspace_,*edit,std::move(value)));},this);
    const auto* source_body=graph.find(initial.fem->source_id);const auto* source_boolean=graph.find_boolean(initial.fem->source_id);
    if(!initial.fem->source_id.empty())dialog->set_source(initial.fem->source_id,
        QString::fromStdString(source_body?source_body->name:source_boolean?source_boolean->name:initial.fem->source_id));
    properties_dialog_=dialog;properties_dialog_instance_path_=prefix;body_dialog_context_=context;body_dialog_step_id_=initial.scope.id;
    // Publish the command's input once. Entry and the independent eye only
    // consume this packet and the immutable source snapshots below.
    preserve_view_on_refresh_=true;
    if(prefix.empty())refresh_scene();
    else refresh_scene([&] {
        kernel::BodyResult input;
        for(const auto& source:context)workspace_detail::append_mesh(input.mesh,sources.at(source).geometry->mesh);
        return workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),
            assembly::InstancePath::decode(prefix),std::move(input));
    });
    auto base=std::make_shared<kernel::ViewerMesh>(viewer_->mesh());
    auto offered=std::make_shared<kernel::ViewerMesh>(*base);
    const auto transform=[this,prefix](auto p){return prefix.empty()?p:workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
    for(const auto& [source_id,source]:sources) {
        const auto& mesh=source.geometry->mesh;const auto offset=static_cast<std::uint32_t>(offered->original_references.vertices.size());
        for(auto p:mesh.vertices)offered->original_references.vertices.push_back(transform(p));
        for(auto index:mesh.triangles)offered->original_references.triangles.push_back(offset+index);
        offered->original_references.triangle_references.insert(offered->original_references.triangle_references.end(),mesh.triangles.size()/3,kernel::FaceReference{source_id,"container:display",prefix});
        for(auto edge:mesh.edges) {
            edge.reference={source_id,"container:display",prefix};edge.display_owner_id=source_id;
            for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
            offered->original_references.edges.push_back(std::move(edge));
        }
    }
    auto offering=std::make_shared<std::optional<bool>>();
    dialog->changed=[this,dialog,sources,prefix,base,offered,offering,transform] {
        const auto accepts=[sources,prefix](const viewer::ViewerCandidate& c){return c.kind==viewer::CandidateKind::Container&&c.instance_path==prefix&&sources.contains(c.owner_id);};
        const bool active=dialog->active_input();
        if(!*offering||**offering!=active){viewer_->set_mesh(active?*offered:*base);*offering=active;}
        viewer_->set_original_container_selection(active);viewer_->set_selection_contract(active?std::vector{viewer::CandidateKind::Container}:std::vector<viewer::CandidateKind>{});
        viewer_->set_candidate_filter(active?std::function<bool(const viewer::ViewerCandidate&)>(accepts):std::function<bool(const viewer::ViewerCandidate&)>([](const auto&){return false;}));
        tree_->setProperty("commandSelectionActive",active);
        feature_reference_pick_={};
        if(active)feature_reference_pick_=[this,dialog,sources,accepts](const auto& candidate) {
            if(!accepts(candidate))return;
            const auto source=candidate.owner_id;const auto label=sources.at(source).name;auto* target=dialog;
            viewer_->clear_selection();tree_->clearSelection();feature_reference_pick_={};target->set_source(source,label);
        };
        const auto source=sources.find(dialog->pending.fem->source_id);
        dialog->set_source_available(source!=sources.end());
        dialog->set_status(source!=sources.end()||dialog->pending.fem->source_id.empty()?QString{}:tr("The FEM source is missing or suppressed."));
        std::vector<kernel::ViewerEdge> inspection;
        if(dialog->inspected()&&source!=sources.end())for(auto edge:source->second.geometry->mesh.edges) {
            edge.overlay=true;edge.color="#00D1FF";
            for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
            inspection.push_back(std::move(edge));
        }
        viewer_->set_transient_edges(std::move(inspection));
        feature_reference_end_=[this,dialog]{auto* target=dialog;feature_reference_pick_={};feature_reference_end_={};target->end_input();};
    };
    connect(dialog,&QDialog::finished,this,[this] {
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();body_dialog_context_.reset();body_dialog_step_id_.clear();
        feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});
        viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->show();dialog->changed();if(initial.fem->source_id.empty())dialog->arm();
}

bool AssemblyWorkspaceWindow::accept_fem_tree_reference(QTreeWidgetItem* item) {
    auto* dialog=dynamic_cast<FemBodyDialog*>(properties_dialog_);
    if(!item||!dialog||!dialog->active_input()||!feature_reference_pick_)return false;
    const auto kind=item->data(0,Qt::UserRole+3).toString();
    if(kind!="part-body"&&kind!="part-body-boolean")return false;
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Container;
    candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();candidate.instance_path=workspace_.active_occurrence_path();
    const auto pick=feature_reference_pick_;pick(candidate);return true;
}
} // namespace zima::app
