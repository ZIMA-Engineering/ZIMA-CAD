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
#include <QTimer>
#include <QVariantAnimation>
#include <QPushButton>
#include <QAction>
#include <cmath>
#include <numbers>
#include <limits>
#include <mutex>
#include <ranges>
#ifdef ZIMA_ENABLE_FEM_MESHER
#include "fem_linear_statics.hpp"
#include "fem_volume_mesh.hpp"
#include "fem_thermal.hpp"
#include "fem_probe.hpp"
#endif

namespace zima::app {
namespace {
#ifdef ZIMA_ENABLE_FEM_MESHER
// The calculation thread publishes only plain values. A UI-thread timer
// consumes the latest snapshot without queueing one event per element.
class FemTaskProgress final {
public:
    explicit FemTaskProgress(std::function<void(const FemProgress&)> update)
        : update_(std::move(update)) {
        QObject::connect(&timer_,&QTimer::timeout,&timer_,[this]{flush();});
        timer_.start(80);
    }
    FemProgressCallback callback() {
        return [this](const FemProgress& value) {
            const std::lock_guard lock(mutex_);
            pending_=value;
        };
    }
    void flush() {
        std::optional<FemProgress> value;
        {const std::lock_guard lock(mutex_);value.swap(pending_);}
        if(value)update_(*value);
    }
    static QString message(const FemProgress& value) {
        switch(value.phase) {
        case FemProgressPhase::PreparingGeometry:return QObject::tr("FEM: preparing geometry…");
        case FemProgressPhase::GeneratingMesh:return QObject::tr("FEM: generating the volume mesh…");
        case FemProgressPhase::CheckingMesh:return QObject::tr("FEM: checking mesh quality…");
        case FemProgressPhase::OptimizingMesh:return QObject::tr("FEM: optimizing the quadratic mesh…");
        case FemProgressPhase::ReadingMesh:return QObject::tr("FEM: preparing mesh data…");
        case FemProgressPhase::AssemblingSystem:return QObject::tr("FEM: assembling the system…");
        case FemProgressPhase::ApplyingConditions:return QObject::tr("FEM: applying loads and restraints…");
        case FemProgressPhase::SolvingSystem:return QObject::tr("FEM: solving the system (iteration %1)…")
            .arg(static_cast<qulonglong>(value.completed));
        case FemProgressPhase::ComputingResults:return QObject::tr("FEM: calculating displacement and stress…");
        case FemProgressPhase::ComputingTemperature:return QObject::tr("FEM: checking temperatures and power balance…");
        case FemProgressPhase::PreparingDisplay:return QObject::tr("FEM: preparing result display…");
        }
        return {};
    }
    static int percent(const FemProgress& value) {
        return value.total?static_cast<int>(std::min(100.,100.*value.completed/value.total)):-1;
    }
private:
    std::function<void(const FemProgress&)> update_;
    std::mutex mutex_;
    std::optional<FemProgress> pending_;
    QTimer timer_;
};

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
auto calculation_conditions(const document::FemDefinition& definition) {
    return definition.conditions|std::views::filter([analysis=definition.analysis,expansion=document::fem_thermal_expansion(definition)](const auto& condition){
        return document::fem_condition_applies_to_analysis(condition.kind,analysis,expansion);
    });
}
bool same_calculation_definition(const document::FemDefinition& a,const document::FemDefinition& b) {
    return a.source_id==b.source_id&&a.analysis==b.analysis&&a.mesh==b.mesh&&a.thermal==b.thermal&&
        std::ranges::equal(calculation_conditions(a),calculation_conditions(b));
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
class FemDeformationSettingsDialog final : public ui::PropertiesSubWindow {
public:
    FemDeformationSettingsDialog(double scale,double speed,double suggested,
            std::function<void(double,double)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Deformation settings"),parent),commit_(std::move(commit)) {
        setObjectName("femDeformationSettingsDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;
        scale_=new QDoubleSpinBox(this);scale_->setObjectName("femMagnification");
        scale_->setRange(0,1000000);scale_->setDecimals(3);scale_->setValue(scale);scale_->setSuffix(" ×");
        form->addRow(tr("Deformation magnification"),scale_);
        auto* automatic=new QPushButton(tr("Fit magnification to model"),this);
        automatic->setObjectName("femAutomaticMagnification");
        connect(automatic,&QPushButton::clicked,this,[this,suggested]{scale_->setValue(suggested);});form->addRow({},automatic);
        speed_=new QDoubleSpinBox(this);speed_->setObjectName("femAnimationSpeed");
        speed_->setRange(.1,10);speed_->setDecimals(2);speed_->setValue(speed);speed_->setSuffix(" ×");
        form->addRow(tr("Animation speed"),speed_);content_layout()->addLayout(form);
        auto* explanation=new QLabel(tr("Animation interpolates the static result. Values and colors always show the calculated load case."),this);
        explanation->setWordWrap(true);content_layout()->addWidget(explanation);set_initial_size({490,240});
    }
protected:
    bool submit()override{commit_(scale_->value(),speed_->value());return true;}
private:
    QDoubleSpinBox *scale_{},*speed_{};std::function<void(double,double)> commit_;
};
}

bool AssemblyWorkspaceWindow::active_fem_body()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->fem.has_value();
}

bool AssemblyWorkspaceWindow::fem_thermal_analysis()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->fem&&body->fem->analysis==document::FemAnalysisKind::SteadyStateThermal;
}

bool AssemblyWorkspaceWindow::fem_expansion_analysis()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->fem&&document::fem_thermal_expansion(*body->fem);
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
        std::vector<double> displacement_field,stress_field;
        double maximum_MPa{};
        std::vector<kernel::Vec3> displacement;
        std::vector<double> nodal_magnitude,element_stress;
        std::optional<FemThermalResult> thermal;
        FemThermalMaterial thermal_material;
        std::optional<FemExpansionMaterial> expansion_material;
        std::vector<double> temperature_field;
        struct Measurement {document::FemCondition condition;std::optional<FemProbeLocation> location;};
        std::map<std::string,Measurement> measurements;
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
    const auto material=workspace::material_data(workspace_,id);
    if(same_calculation_definition(calculated.definition,definition)&&
            (calculated.thermal?calculated.thermal_material==fem_thermal_material(material):calculated.material==elastic_material(material))&&
            (!calculated.expansion_material||(calculated.material==elastic_material(material)&&
                *calculated.expansion_material==fem_expansion_material(material))))
        return &calculated.deformation;
#endif
    return nullptr;
}

void AssemblyWorkspaceWindow::solve_fem_statics() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(properties_dialog_||!active_fem_body())return;
    bool progress_started=false;
    try {
        if(!active_generated_fem_mesh())throw std::runtime_error("Generate the FEM mesh before solving.");
        const auto mesh=generated_fem_mesh_;
        const auto& id=workspace_.active_document_id();
        const auto& graph=workspace_.open_part(id)->session.document().body_history;
        const auto definition=*graph.find(graph.active_body_id())->fem;
        const auto material=elastic_material(workspace::material_data(workspace_,id));
        const auto thermal_material=fem_thermal_material(workspace::material_data(workspace_,id));
        const auto expansion_material=fem_expansion_material(workspace::material_data(workspace_,id));
        if(!active_fem_deformation()){
            begin_status_operation(tr("FEM: assembling the system…"));
            progress_started=true;
            FemTaskProgress progress([this](const FemProgress& value) {
                update_status_operation(FemTaskProgress::message(value),FemTaskProgress::percent(value),value.total?100:0);
            });
            auto solution=ui::run_background_task([mesh,definition,material,thermal_material,expansion_material,report=progress.callback()]{
                GeneratedFemMesh::Solution calculated;calculated.definition=definition;calculated.material=material;
                if(definition.analysis==document::FemAnalysisKind::SteadyStateThermal) {
                    calculated.thermal=solve_fem_steady_thermal(mesh->mesh,definition,thermal_material,report);
                    calculated.thermal_material=thermal_material;calculated.deformation=mesh->mesh.surface;
                    calculated.deformation.edges.clear();calculated.displacement.resize(mesh->mesh.surface.vertices.size());
                    report_fem_progress(report,FemProgressPhase::PreparingDisplay);
                    const auto& triangles=mesh->mesh.surface.triangles;
                    for(std::size_t i=0;i<triangles.size();i+=3) {
                        double temperature{};for(unsigned j=0;j<3;++j)temperature+=calculated.thermal->temperature_C.at(triangles[i+j])/3;
                        calculated.temperature_field.push_back(temperature);
                    }
                    if(!document::fem_thermal_expansion(definition))return calculated;
                    calculated.expansion_material=expansion_material;
                }
                const FemThermalStrain strain{expansion_material,calculated.thermal?
                    std::span<const double>(calculated.thermal->temperature_C):std::span<const double>{}};
                const auto result=solve_fem_linear_statics(mesh->mesh,definition,material,report,calculated.expansion_material?&strain:nullptr);
                report_fem_progress(report,FemProgressPhase::PreparingDisplay);
                calculated.deformation=fem_deformed_mesh(mesh->mesh,result);calculated.maximum_mm=result.maximum_displacement_mm;
                const auto& triangles=mesh->mesh.surface.triangles;
                for(std::size_t t=0;t<triangles.size()/3;++t) {
                    double displacement=0;
                    for(unsigned c=0;c<3;++c) {
                        const auto u=result.displacement.at(triangles[t*3+c]);
                        displacement+=std::hypot(u.x,u.y,u.z)/3;
                    }
                    calculated.displacement_field.push_back(displacement);
                    calculated.stress_field.push_back(result.element_von_mises_MPa.at(mesh->mesh.surface_elements.at(t)));
                }
                calculated.maximum_MPa=result.maximum_von_mises_MPa;
                calculated.displacement=result.displacement;
                calculated.element_stress=result.element_von_mises_MPa;
                for(const auto u:result.displacement)calculated.nodal_magnitude.push_back(std::hypot(u.x,u.y,u.z));
                return calculated;
            });
            progress.flush();
            mesh->solution=std::move(solution);
            fem_probe_selection_.reset();
            if(fem_animation_)fem_animation_->stop();
            fem_animation_fraction_=1;
            fem_result_quantity_=mesh->solution->thermal?FemResultQuantity::Temperature:FemResultQuantity::Displacement;
        }
        if(!active_fem_deformation()) {
            if(progress_started)finish_status_operation(tr("FEM inputs changed. Repeat the operation."),false);
            return;
        }
        show_fem_deformation_=true;
        preserve_view_on_refresh_=true;
        refresh_scene();
        rebuild_application_toolbar();
        update_fem_result_presentation();
        if(progress_started)finish_status_operation(state_->text());
    }catch(const std::exception& error){
        if(progress_started)finish_status_operation(tr(error.what()),false);
        else state_->setText(tr(error.what()));
    }
#endif
}

void AssemblyWorkspaceWindow::update_fem_result_presentation() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(!show_fem_deformation_||properties_dialog_||!active_sketch_id_.empty()||!active_fem_deformation()) {
        viewer_->set_triangle_scalar_field({});viewer_->set_result_displacements({});
        set_fem_animation(false);return;
    }
    const auto& result=*generated_fem_mesh_->solution;
    const bool stress=fem_result_quantity_==FemResultQuantity::VonMises;
    const bool temperature=fem_result_quantity_==FemResultQuantity::Temperature&&result.thermal;
    if(temperature)viewer_->set_triangle_scalar_field(result.temperature_field,tr("Temperature [°C]"),result.thermal->minimum_C,result.thermal->maximum_C);
    else viewer_->set_triangle_scalar_field(stress?result.stress_field:result.displacement_field,
        stress?tr("Von Mises stress [MPa]"):tr("Displacement [mm]"),0,stress?result.maximum_MPa:result.maximum_mm);
    viewer_->set_result_displacements(result.displacement);
    viewer_->set_result_probe(fem_probe_enabled_?std::function<void(const viewer::MeshView::ResultHit&)>{[this](const auto& hit) {
        fem_probe_selection_=std::pair{hit.triangle,hit.node};update_fem_result_markers();
    }}:std::function<void(const viewer::MeshView::ResultHit&)>{});
    update_fem_deformation();update_fem_result_markers();
    if(temperature)state_->setText(tr("Temperature: %1 to %2 °C · input %3 W · output %4 W")
        .arg(result.thermal->minimum_C,0,'g',6).arg(result.thermal->maximum_C,0,'g',6)
        .arg(result.thermal->input_W,0,'g',6).arg(result.thermal->output_W,0,'g',6));
    else state_->setText(stress?tr("Maximum von Mises stress (element average): %1 MPa").arg(result.maximum_MPa,0,'g',6):
        tr("Maximum displacement: %1 mm").arg(result.maximum_mm,0,'g',6));
#endif
}

bool AssemblyWorkspaceWindow::fem_animation_running()const {
    return fem_animation_&&fem_animation_->state()==QAbstractAnimation::Running;
}
void AssemblyWorkspaceWindow::set_fem_animation(bool run) {
    if(!run) {
        if(fem_animation_running())fem_animation_->pause();
        return;
    }
    if(!active_fem_deformation()||properties_dialog_||(fem_thermal_analysis()&&!fem_expansion_analysis()))return;
    if(!show_fem_deformation_) {show_fem_deformation_=true;preserve_view_on_refresh_=true;refresh_scene();}
    if(!fem_animation_) {
        fem_animation_=new QVariantAnimation(this);
        fem_animation_->setStartValue(0.);fem_animation_->setKeyValueAt(.5,1.);fem_animation_->setEndValue(0.);
        fem_animation_->setLoopCount(-1);
        connect(fem_animation_,&QVariantAnimation::valueChanged,this,[this](const QVariant& value) {
            if(viewer_->triangle_scalar_field().empty()||!viewer_->isVisible()) {set_fem_animation(false);return;}
            fem_animation_fraction_=.5-.5*std::cos(value.toDouble()*std::numbers::pi);
            update_fem_deformation();
        });
    }
    fem_animation_->setDuration(static_cast<int>(3000/fem_animation_speed_));
    if(fem_animation_->state()==QAbstractAnimation::Paused)fem_animation_->resume();
    else {fem_animation_fraction_=0;update_fem_deformation();fem_animation_->start();}
}
void AssemblyWorkspaceWindow::update_fem_deformation() {
    if(fem_thermal_analysis()&&!fem_expansion_analysis()) {
        viewer_->set_result_deformation(1,false);viewer_->set_result_caption(tr("Steady-state temperature"));return;
    }
    viewer_->set_result_deformation(fem_magnification_*fem_animation_fraction_,fem_show_original_);
    viewer_->set_result_caption((fem_expansion_analysis()?tr("Thermal deformation ×%1 · static result"):
        tr("Deformation ×%1 · static result")).arg(fem_magnification_*fem_animation_fraction_,0,'g',4));
}
void AssemblyWorkspaceWindow::update_fem_result_markers() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(!active_fem_deformation())return;
    auto& solved=*generated_fem_mesh_->solution;const auto& mesh=generated_fem_mesh_->mesh;
    const bool stress=fem_result_quantity_==FemResultQuantity::VonMises;
    const bool temperature=fem_result_quantity_==FemResultQuantity::Temperature&&solved.thermal;
    std::vector<viewer::MeshView::ResultMarker> markers;
    const auto marker=[&](std::size_t index,const QString& prefix) {
        viewer::MeshView::ResultMarker result;
        if(stress) {
            for(unsigned i=0;i<4;++i) {
                const auto node=mesh.tetrahedra.at(index)[i];
                result.position=kernel::dimension_add(result.position,kernel::dimension_scale(solved.deformation.vertices[node],.25));
                result.displacement=kernel::dimension_add(result.displacement,kernel::dimension_scale(solved.displacement[node],.25));
            }
        }else {result.position=solved.deformation.vertices.at(index);result.displacement=solved.displacement.at(index);}
        result.label=prefix+" "+locale().toString(temperature?solved.thermal->temperature_C.at(index):
            stress?solved.element_stress.at(index):solved.nodal_magnitude.at(index),'g',6)+(temperature?" °C":stress?" MPa":" mm");
        markers.push_back(std::move(result));
    };
    if(fem_show_extrema_) {
        const auto& values=temperature?solved.thermal->temperature_C:stress?solved.element_stress:solved.nodal_magnitude;
        if(!values.empty()) {
            const auto [low,high]=std::minmax_element(values.begin(),values.end());
            marker(low-values.begin(),tr("Minimum:"));marker(high-values.begin(),tr("Maximum:"));
        }
    }
    if(fem_probe_enabled_&&fem_probe_selection_) {
        const auto [triangle,node]=*fem_probe_selection_;
        if(triangle<mesh.surface_elements.size()&&node<solved.displacement.size()) {
            marker(stress?mesh.surface_elements[triangle]:node,stress?tr("Element average:"):tr("Mesh node:"));
            state_->setText(markers.back().label);
        }
    }
    const auto& graph=workspace_.open_part(workspace_.active_document_id())->session.document().body_history;
    for(const auto& condition:graph.find(graph.active_body_id())->fem->conditions)
        if(condition.kind==document::FemConditionKind::MeasurementPoint&&condition.visible) {
            const kernel::Vec3 point{condition.placement.x,condition.placement.y,condition.placement.z};
            auto found=solved.measurements.find(condition.id);
            if(found==solved.measurements.end()||found->second.condition!=condition) {
                found=solved.measurements.insert_or_assign(condition.id,GeneratedFemMesh::Solution::Measurement{
                    condition,condition.placement.reference_valid?locate_fem_point(mesh,point):std::nullopt}).first;
            }
            viewer::MeshView::ResultMarker measurement;measurement.position=point;
            measurement.label=QString::fromStdString(condition.name)+": ";
            if(!condition.placement.reference_valid)measurement.label+=tr("The condition placement is unresolved.");
            else if(!found->second.location)measurement.label+=tr("Measurement point is outside the FEM volume.");
            else {
                if(solved.thermal)measurement.label+=locale().toString(fem_probe_scalar(mesh,*found->second.location,solved.thermal->temperature_C),'g',6)+" °C";
                if(!solved.thermal||solved.expansion_material) {
                    if(solved.thermal)measurement.label+=" · ";
                    const auto u=fem_probe_displacement(mesh,*found->second.location,solved.displacement);
                    measurement.displacement=u;measurement.position=kernel::dimension_add(point,u);
                    measurement.label+=tr("ΔX %1 · ΔY %2 · ΔZ %3 · total %4 mm")
                        .arg(locale().toString(u.x,'g',6),locale().toString(u.y,'g',6),locale().toString(u.z,'g',6),locale().toString(std::hypot(u.x,u.y,u.z),'g',6));
                }
            }
            markers.push_back(std::move(measurement));
        }
    viewer_->set_result_markers(std::move(markers));
#endif
}
void AssemblyWorkspaceWindow::show_fem_deformation_settings() {
#ifdef ZIMA_ENABLE_FEM_MESHER
    if(properties_dialog_||!active_fem_deformation()||(fem_thermal_analysis()&&!fem_expansion_analysis()))return;
    const bool was_running=fem_animation_running();
    set_fem_animation(false);
    const auto& solved=*generated_fem_mesh_->solution;
    kernel::Vec3 low=solved.deformation.vertices.front(),high=low;
    for(const auto p:generated_fem_mesh_->mesh.surface.vertices) {
        low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
        high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
    }
    const double span=std::hypot(high.x-low.x,high.y-low.y,high.z-low.z);
    const double suggested=solved.maximum_mm>0?std::clamp(.1*span/solved.maximum_mm,1.,1000000.):1.;
    auto* dialog=new FemDeformationSettingsDialog(fem_magnification_,fem_animation_speed_,suggested,[this](double scale,double speed) {
        if(fem_magnification_==scale&&fem_animation_speed_==speed)return;
        const bool scale_changed=fem_magnification_!=scale;
        fem_magnification_=scale;fem_animation_speed_=speed;fem_animation_fraction_=1;
        if(fem_animation_)fem_animation_->stop();
        update_fem_deformation();
        if(scale_changed) {
            const auto& result=*generated_fem_mesh_->solution;
            std::vector<kernel::Vec3> bounds=generated_fem_mesh_->mesh.surface.vertices;
            for(std::size_t i=0;i<result.displacement.size();++i)
                bounds.push_back(kernel::dimension_add(result.deformation.vertices[i],kernel::dimension_scale(result.displacement[i],scale-1)));
            viewer_->fit_points(bounds);
        }
    },this);
    properties_dialog_=dialog;
    connect(dialog,&QDialog::finished,this,[this,was_running](int result) {
        properties_dialog_=nullptr;
        if(result==QDialog::Rejected&&was_running)set_fem_animation(true);
        rebuild_application_toolbar();
    });
    rebuild_application_toolbar();dialog->show();
#endif
}

void AssemblyWorkspaceWindow::select_fem_result(FemResultQuantity quantity) {
    if(!active_fem_deformation()||properties_dialog_)return;
    if(quantity==FemResultQuantity::Temperature?!fem_thermal_analysis():(fem_thermal_analysis()&&!fem_expansion_analysis()))return;
    fem_result_quantity_=quantity;
    if(!show_fem_deformation_) {show_fem_deformation_=true;preserve_view_on_refresh_=true;refresh_scene();}
    else update_fem_result_presentation();
    rebuild_application_toolbar();
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
    if(!std::ranges::equal(calculation_conditions(*body->fem),
        calculation_conditions(generated_fem_mesh_->definition),same_region))return nullptr;
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
    bool progress_started=false;
    try {
        if(!active_generated_fem_mesh()) {
            const auto source=workspace::read_fem_source(session,body_id);
            const auto definition=*graph.find(body_id)->fem;
            begin_status_operation(tr("FEM: preparing geometry…"));
            progress_started=true;
            FemTaskProgress progress([this](const FemProgress& value) {
                update_status_operation(FemTaskProgress::message(value),FemTaskProgress::percent(value),value.total?100:0);
            });
            auto mesh=ui::run_background_task([source,definition,report=progress.callback()]{
                return generate_fem_volume_mesh(source.get(),definition,report);
            });
            progress.flush();
            generated_fem_mesh_=std::make_shared<GeneratedFemMesh>(GeneratedFemMesh{
                document_id,body_id,source,definition,std::move(mesh)});
            if(!active_generated_fem_mesh()) {
                finish_status_operation(tr("FEM inputs changed. Repeat the operation."),false);
                return;
            }
        }
        show_generated_fem_mesh_=true;
        show_fem_deformation_=false;
        preserve_view_on_refresh_=true;
        refresh_scene();
        rebuild_application_toolbar();
        if(progress_started)finish_status_operation(tr("FEM mesh generated."));
    }catch(const std::exception& error){
        if(progress_started)finish_status_operation(tr(error.what()),false);
        else state_->setText(tr(error.what()));
    }
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
