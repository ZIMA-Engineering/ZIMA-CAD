#include "sweep_test_support.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace zima;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
template<class F> double timed(F&& fn) {const auto start=Clock::now();fn();return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static TopoDS_Shape shape(const kernel::BodyResult& result) {
    TopoDS_Shape value;BRep_Builder builder;std::istringstream stream(result.kernel_shape);BRepTools::Read(value,stream,builder);
    if(value.IsNull())throw std::runtime_error("Missing solid snapshot");return value;
}
// Tessellation vertices lie on the calculated surface. Distances are evaluated
// against the other exact trimmed BRep, not against its display triangles.
// This is a sampled comparison, not a certified global Hausdorff bound.
static Json deviation(const kernel::BodyResult& source,const kernel::BodyResult& target) {
    const auto solid=shape(target);double maximum=0,sum=0;std::size_t count=0;
    const auto& points=source.mesh.vertices;
    for(std::size_t i=0;i<points.size();i+=std::max<std::size_t>(1,points.size()/250)) {
        const auto p=points[i];BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(p.x,p.y,p.z)).Shape(),solid);
        if(!distance.IsDone())throw std::runtime_error("Surface distance failed");
        const double d=distance.Value();maximum=std::max(maximum,d);sum+=d*d;++count;
    }
    return {{"samples",count},{"max_mm",maximum},{"rms_mm",std::sqrt(sum/count)}};
}
static document::HistoryContainer fixture(document::FeatureKind kind) {
    auto c=test_support::sweep_fixture(kind);
    if(kind==document::FeatureKind::HelicalSweep) {
        auto guide=sketcher::Sketch::from_serialized(c.helical.sketches[1]);
        for(auto& p:guide.points)if(p.y>0)p.y=40; // eight turns at the default 5 mm pitch
        c.helical.sketches[1]=guide.serialized();
    } else if(kind==document::FeatureKind::Sweep2D) {
        auto path=sketcher::Sketch::create_default();
        static_cast<void>(path.add_arc(30,0,0,0,30,30,false,1e-6,true));
        c.sweep2d.path_sketch=path.serialized();c.sweep2d.profiles.clear();
        document::PartDocument::reframe_sweep2d_sketches(c);
        const auto station=document::PartDocument::sweep2d_route(c).stations.front();
        const auto index=document::PartDocument::ensure_sweep2d_profile(c,station.point_id,station.incoming);
        auto profile=sketcher::Sketch::from_serialized(c.sweep2d.profiles[index].sketch_serialized);
        static_cast<void>(profile.add_circle(0,0,2));c.sweep2d.profiles[index].sketch_serialized=profile.serialized();
    } else {
        for(const auto v:{kernel::Vec3{25,0,20},kernel::Vec3{25,25,35}}) {
            auto p=document::PartDocument::create_construction(document::ConstructionKind::Point);
            p.parent_construction_id=c.sweep3d.path.id;p.origin=v;c.sweep3d.path.curve_points.push_back(p);
        }
        c.sweep3d.path.curve_rounding_enabled=true;
        c.sweep3d.path.curve_points[1].curve_radius=5;c.sweep3d.path.curve_points[2].curve_radius=5;
    }
    return c;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        if(argc<2)throw std::runtime_error("Usage: sweep_precision_benchmark OUTPUT_DIRECTORY [FINE_MM COARSE_MM [helical]]");
        const std::array tolerances{argc>2?document::parse_sweep_tolerance(argv[2]):.001,
            argc>3?document::parse_sweep_tolerance(argv[3]):.1};
        const auto directory=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(directory);
        Json report;report["method"]="Release; one warm-up plus three alternating fresh-kernel runs. Same feature identities and input parameters. Boolean tolerance fixed at 0.001 mm; mesh deflection fixed at 0.1 mm. Request preparation and complete kernel calculation (including meshing and reference packets) timed separately. Bidirectional sampled surface deviation is not a global bound. Screenshots use the real viewer and identical camera.";
        viewer::MeshView view;view.resize(800,700);view.show();app.processEvents();
        for(const auto kind:{document::FeatureKind::Sweep2D,document::FeatureKind::Sweep3D,document::FeatureKind::HelicalSweep}) {
            if(argc>4&&std::string_view(argv[4])=="helical"&&kind!=document::FeatureKind::HelicalSweep)continue;
            const std::string name=kind==document::FeatureKind::Sweep2D?"sweep2d":kind==document::FeatureKind::Sweep3D?"sweep3d":"helical";
            auto doc=document::PartDocument::create_default();doc.history={fixture(kind)};
            Json entry;entry["name"]=name;std::array<kernel::BodyResult,2> results;
            for(int cycle=-1;cycle<3;++cycle)for(int step=0;step<2;++step) {
                const int index=cycle>=0&&cycle%2?1-step:step;const double tolerance=tolerances[index];
                doc.history.front().sweep_precision.custom_tolerance=tolerance;doc.document_precision["linear_tolerance"]="0.001";doc.document_precision["mesh_deflection"]="0.1";
                std::vector<kernel::HistoryOperation> operations;
                const auto preparation=timed([&]{operations=doc.kernel_operations();});
                for(auto& operation:operations)operation.boolean_tolerance=.001;
                kernel::OcctKernel kernel;std::vector<kernel::BodyResult> calculated;
                const auto calculation=timed([&]{calculated=kernel.evaluate_history(operations);});
                if(calculated.empty()||!calculated.back().calculation_errors.empty())throw std::runtime_error("Sweep calculation failed");
                const auto& request=std::get<kernel::Sweep3DRequest>(operations.back().primitive);
                results[index]=std::move(calculated.back());const auto& result=results[index];
                if(cycle>=0)entry["samples"].push_back({{"cycle",cycle},{"tolerance_mm",tolerance},{"prepare_ms",preparation},{"kernel_ms",calculation},
                    {"path_segments",request.path_segments.size()},{"triangles",result.mesh.triangles.size()/3},{"volume_mm3",result.volume},{"area_mm2",result.surface_area},
                    {"valid_brep",bool(BRepCheck_Analyzer(shape(result)).IsValid())}});
                std::cout<<name<<" cycle="<<cycle<<" tolerance="<<tolerance<<" ms="<<calculation<<std::endl;
            }
            entry["fine_to_coarse"]=deviation(results[0],results[1]);entry["coarse_to_fine"]=deviation(results[1],results[0]);
            entry["volume_delta_percent"]=(results[1].volume/results[0].volume-1)*100;
            QImage comparison(1600,735,QImage::Format_RGB32);comparison.fill(Qt::white);QPainter painter(&comparison);
            view.set_mesh(results[0].mesh);view.fit_all();app.processEvents();const auto camera=view.camera_state();
            for(int i=0;i<2;++i) {
                view.set_mesh(results[i].mesh,false);view.set_camera_state(camera);app.processEvents();view.repaint();app.processEvents();
                const auto image=view.grabFramebuffer();if(image.isNull())throw std::runtime_error("Viewer screenshot failed");
                painter.drawImage(QRect(i*800,35,800,700),image);painter.drawText(i*800+20,24,QString::fromStdString(name)+QString(" / %1 mm").arg(tolerances[i]));
            }
            painter.end();comparison.save(QString::fromStdString((directory/(name+".png")).string()));
            report["fixtures"].push_back(entry);
            std::ofstream out(directory/"report.json");out<<report.dump(2);
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
