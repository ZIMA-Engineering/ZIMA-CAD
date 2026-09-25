#include "profile_solid_fixture.hpp"
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QImage>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace zima;
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
template<class F> double timed(F&& function){const auto start=Clock::now();function();return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try {
        const std::array<std::string,2> tolerances{argc>3?argv[3]:"0.01",argc>4?argv[4]:"0.001"};
        for(const auto& value:tolerances)require(std::isfinite(std::stod(value))&&std::stod(value)>0,"Tolerance must be finite and positive");
        require(std::stod(tolerances[0])!=std::stod(tolerances[1]),"Choose two different calculation tolerances");
        kernel::OcctKernel kernel;Json report;report["linear_tolerances_mm"]={std::stod(tolerances[0]),std::stod(tolerances[1])};report["mesh_deflection_mm"]=.1;
        report["method"]="Release build; one warm-up and five alternating measured cycles; original models are never saved. Times include native transactions; draw_ms is CPU sheet painting to QImage, not interactive GPU frame latency.";
        for(const int count:{1,12}) {
            workspace::Workspace live;auto part=document::PartDocument::create_default();
            for(int i=0;i<count;++i){auto feature=zima::test::circular_feature(part,10+i*.1,20);feature.placement.x=i*30.;part.history.push_back(feature);}
            auto calculated=workspace::calculate_part_with_resolved_references(kernel,part,nullptr,{true});
            const auto id=part.document_id;live.add_part(part,calculated);
            auto assembly=assembly::AssemblyDocument::create_default();
            for(int i=0;i<8;++i){auto item=assembly::AssemblyDocument::create_part_occurrence("Component",id,{},calculated.back());item.placement.x=i*count*32.;assembly.components.push_back(std::move(item));}
            const auto assembly_id=assembly.document_id;live.add_assembly(assembly);
            auto drawing=drawing::DrawingDocument::create_default();
            for(const auto orientation:{drawing::ViewOrientation::Front,drawing::ViewOrientation::Top,drawing::ViewOrientation::Isometric})
                drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(id,{},calculated.back().mesh,orientation));
            const auto baseline_volume=calculated.back().volume;
            Json samples=Json::array();
            for(int cycle=-1;cycle<5;++cycle)for(const auto& tolerance:tolerances) {
                Json sample;sample["cycle"]=cycle;sample["tolerance_mm"]=std::stod(tolerance);
                auto settings=workspace::file_settings(live,id);settings.precision["linear_tolerance"]=tolerance;
                workspace::SettingsChange changed;
                sample["part_settings_ms"]=timed([&]{changed=workspace::set_file_settings(live,kernel,id,settings);});sample["part_recalculated"]=changed.calculated;
                const auto* state=live.open_part(id);const auto& result=state->session.calculated_boundaries().back();
                sample["volume_mm3"]=result.volume;sample["volume_delta_mm3"]=result.volume-baseline_volume;sample["triangles"]=result.mesh.triangles.size()/3;
                require(std::abs(result.volume-baseline_volume)<std::max(1e-6,baseline_volume*1e-8),"Precision benchmark changed separated cylinders' exact volume");
                require(zima::test::circular_radius(state->session.document(),state->session.document().history.front())==10,"Precision change altered a modeling parameter");
                sample["dependency_refresh_ms"]=timed([&]{live.refresh_source_geometry();});
                auto as=workspace::file_settings(live,assembly_id);as.precision["linear_tolerance"]=tolerance;
                sample["assembly_settings_ms"]=timed([&]{changed=workspace::set_file_settings(live,kernel,assembly_id,as);});sample["assembly_recalculated"]=changed.calculated;
                sample["assembly_regenerate_ms"]=timed([&]{workspace::regenerate_assembly(live,kernel,assembly_id);});
                sample["assembly_scene_ms"]=timed([&]{const auto mesh=live.authoritative_viewer_mesh(assembly_id);require(!mesh.triangles.empty(),"Assembly scene is empty");});
                sample["drawing_regenerate_ms"]=timed([&]{static_cast<void>(workspace::regenerate_drawing_views(drawing,&live,{}));});
                QImage image(1200,1600,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
                sample["drawing_paint_ms"]=timed([&]{drawing_render::SheetRenderer painter;painter.set_render_sheet(&drawing.sheets.front());QPainter output(&image);painter.paint_sheet(output,4,{},true);});
                if(cycle>=0)samples.push_back(std::move(sample));
            }
            report["fixtures"].push_back({{"name",count==1?"one cylinder / eight occurrences":"twelve disjoint cylinders / eight occurrences"},{"samples",samples}});
            auto cut_model=live.open_assembly(assembly_id)->session.document();
            auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(-1,-12,1,12));
            auto cutter=document::PartDocument::create_extrusion_container(profile.id);
            cutter.combine_mode=document::CombineMode::Subtract;cutter.extrusion.length_forward=30;cutter.extrusion.height=30;
            profile.owner_container_id=cutter.id;cut_model.sketches.push_back(profile);
            cut_model.cuts.push_back({cutter,{cut_model.components.front().occurrence_id},{}});
            workspace::calculate_resolved_assembly_cuts(kernel,cut_model);live.open_assembly(assembly_id)->session.commit(cut_model);
            Json cut_samples=Json::array();
            for(int cycle=-1;cycle<5;++cycle)for(const auto& tolerance:tolerances) {
                auto settings=workspace::file_settings(live,assembly_id);settings.precision["linear_tolerance"]=tolerance;workspace::SettingsChange changed;
                const auto ms=timed([&]{changed=workspace::set_file_settings(live,kernel,assembly_id,settings);});
                require(changed.calculated,"Assembly precision did not recalculate its own cut");
                if(cycle>=0)cut_samples.push_back({{"cycle",cycle},{"tolerance_mm",std::stod(tolerance)},{"assembly_cut_settings_ms",ms},{"volume_mm3",live.open_assembly(assembly_id)->session.document().components.front().calculated_source->volume}});
            }
            report["fixtures"].push_back({{"name",count==1?"one cylinder / assembly-owned cut":"twelve cylinders / assembly-owned cut"},{"samples",cut_samples}});
        }
        // An optional real Part broadens the geometry workload without changing
        // its file, dependencies, parameters or user's application settings.
        if(argc>2) {
            std::vector<kernel::BodyResult> boundaries;auto part=document::PartDocument::load(std::filesystem::u8path(argv[2]),&boundaries);
            workspace::Workspace live;const auto id=part.document_id;live.add_part(std::move(part),std::move(boundaries),std::filesystem::u8path(argv[2]));
            Json samples=Json::array();
            for(int cycle=-1;cycle<5;++cycle)for(const auto& tolerance:tolerances) {
                auto settings=workspace::file_settings(live,id);settings.precision["linear_tolerance"]=tolerance;workspace::SettingsChange changed;
                const auto ms=timed([&]{changed=workspace::set_file_settings(live,kernel,id,settings);});const auto& result=live.open_part(id)->session.calculated_boundaries().back();
                if(cycle>=0) {
                    Json stats=Json::array();
                    for(const auto& boundary:live.open_part(id)->session.calculated_boundaries()) {
                        Json owners=Json::object();
                        for(const auto& face:boundary.mesh.triangle_references) {
                            auto& count=owners[face.owner_id];if(count.is_null())count=0;count=count.get<int>()+1;
                        }
                        stats.push_back({{"triangles",boundary.mesh.triangles.size()/3},{"volume_mm3",boundary.volume},{"triangles_by_owner",owners},{"calculation_errors",boundary.calculation_errors}});
                    }
                    samples.push_back({{"cycle",cycle},{"tolerance_mm",std::stod(tolerance)},{"part_settings_ms",ms},{"part_recalculated",changed.calculated},{"volume_mm3",result.volume},{"triangles",result.mesh.triangles.size()/3},{"boundaries",stats}});
                }
            }
            report["fixtures"].push_back({{"name","user Part (read-only input)"},{"samples",samples}});
            Json operation_samples=Json::array();
            for(int cycle=-1;cycle<5;++cycle)for(const auto& tolerance:tolerances) {
                auto settings=workspace::file_settings(live,id);settings.precision["linear_tolerance"]=tolerance;
                static_cast<void>(workspace::set_file_settings(live,kernel,id,settings));
                const auto& model=live.open_part(id)->session.document();
                auto operations=model.kernel_operations();std::vector<kernel::BodyResult> previous;
                for(std::size_t i=0;i<operations.size();++i) {
                    auto prefix=operations;prefix.resize(i+1);std::vector<kernel::BodyResult> calculated;
                    const auto ms=timed([&]{calculated=kernel.evaluate_history_incremental(prefix,previous);});
                    require(calculated.back().calculation_errors.empty(),"Per-operation benchmark encountered a failed feature");
                    if(cycle>=0)operation_samples.push_back({{"cycle",cycle},{"tolerance_mm",std::stod(tolerance)},{"operation_index",i},{"incremental_kernel_ms",ms},{"triangles",calculated.back().mesh.triangles.size()/3}});
                    previous=std::move(calculated);
                }
            }
            report["incremental_kernel_operations"]=operation_samples;
        }
        const auto text=report.dump(2);if(argc>1){std::ofstream out(std::filesystem::u8path(argv[1]));out<<text;require(bool(out),"Cannot write precision report");}else std::cout<<text;
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
