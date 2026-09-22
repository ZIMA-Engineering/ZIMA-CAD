#include "dxf_spline_import_test_support.hpp"
#include "dxf_export_test_support.hpp"
#include <zima/interchange/dxf.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-8,"Spline differs from expected exact geometry");}
void same_curve(const kernel::BSplineGeometry& first,const kernel::BSplineGeometry& second) {
    for(int i=0;i<=160;++i){const auto a=kernel::bspline_value(first,i/160.),b=kernel::bspline_value(second,i/160.);near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
}
void math_cases() {
    for(unsigned degree=1;degree<=5;++degree)for(int ends=0;ends<4;++ends) {
        kernel::BSplineGeometry source;source.degree=degree;const auto n=degree+5;
        for(unsigned i=0;i<n;++i){source.poles.push_back({double(i),std::sin(i*.7),double(i%3)});source.weights.push_back(1+(i%3)*.4);}
        for(unsigned i=0;i<n+degree+1;++i)source.knots.push_back(i+(i%3)*.15);
        if(ends&1)std::fill(source.knots.begin(),source.knots.begin()+degree+1,source.knots[degree]);
        if(ends&2)std::fill(source.knots.begin()+n,source.knots.end(),source.knots[n]);
        const auto clamped=sketcher::clamp_curve_geometry(source);clamped.validate();same_curve(source,clamped);
        require(sketcher::clamp_curve_geometry(clamped)==clamped,"Clamping changed an already native spline");
        const auto trimmed=sketcher::trim_curve_geometry(clamped,.13,.87);
        for(int i=0;i<=20;++i){const auto p=kernel::bspline_value(trimmed,i/20.),q=kernel::bspline_value(source,.13+.74*i/20.);near(p.x,q.x);near(p.y,q.y);near(p.z,q.z);}
    }
    const auto original=test::unclamped_dxf_curves().front();std::vector<kernel::BSplineGeometry> invalid;
    auto c=original;c.degree=0;invalid.push_back(c);c=original;c.weights.pop_back();invalid.push_back(c);
    c=original;c.weights[1]=-1;invalid.push_back(c);c=original;c.poles[1].x=std::numeric_limits<double>::infinity();invalid.push_back(c);
    c=original;std::swap(c.knots[2],c.knots[3]);invalid.push_back(c);c=original;c.knots[3]=c.knots[2];invalid.push_back(c);
    c=original;c.knots[0]=std::numeric_limits<double>::quiet_NaN();invalid.push_back(c);
    c={2,{{0,0,0},{1,0,0},{2,1,0},{3,1,0},{4,0,0},{5,0,0}},{0,1,2,3,3,3,4,5,6},{1,1,1,1,1,1}};invalid.push_back(c);
    for(const auto& bad:invalid){bool rejected=false;try{static_cast<void>(sketcher::clamp_curve_geometry(bad));}catch(const std::exception&){rejected=true;}require(rejected,"Invalid spline data reached knot insertion");}
}
void import_cases(const kernel::OcctKernel& kernel,const fs::path& dir) {
    test::write_unclamped_dxf(dir/"splines.dxf");auto sketch=sketcher::Sketch::create_default();sketch.plane=sketcher::SketchPlane::XZ;const auto id=sketch.id;
    const auto report=interchange::import_dxf(dir/"splines.dxf",sketch,10);
    require(report.imported_entities==4&&report.warnings.empty()&&sketch.id==id&&sketch.plane==sketcher::SketchPlane::XZ,"Spline import lost receipt or input plane");test::check_unclamped_dxf_sketch(sketch);
    test::check_unclamped_dxf_sketch(sketcher::Sketch::from_serialized(sketch.serialized()));
    interchange::export_dxf(dir/"roundtrip.dxf",sketch);auto roundtrip=sketcher::Sketch::create_default();static_cast<void>(interchange::import_dxf(dir/"roundtrip.dxf",roundtrip));test::check_unclamped_dxf_sketch(roundtrip);
    const auto first=sketch.points;const auto second=interchange::import_dxf(dir/"splines.dxf",sketch);sketch.transform_import_block(second.import_block_id,11,-3,.25);
    for(const auto& point:first)require(*sketch.find_point(point.id)==point,"Periodic spline reused an older block point");
    for(bool closed:{false,true}) {
        std::ofstream out(dir/"invalid.dxf");out<<"0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n0\n20\n0\n11\n1\n21\n1\n";
        auto bad=test::unclamped_dxf_curves().front();if(!closed)bad.knots[3]=1;
        test::write_control_spline(out,bad,closed?11:8);out<<"0\nENDSEC\n0\nEOF\n";out.close();
        const auto before=sketch.serialized();bool rejected=false;try{static_cast<void>(interchange::import_dxf(dir/"invalid.dxf",sketch));}catch(const std::exception&){rejected=true;}
        require(rejected&&sketch.serialized()==before,"Invalid spline partly committed the preceding line");
    }
    // Four quadratic arcs round a 4 x 4 square; each removes exactly 2/3 mm^2.
    std::ofstream out(dir/"periodic.dxf");out<<"0\nSECTION\n2\nENTITIES\n";test::write_control_spline(out,test::unclamped_dxf_curves()[2],11);out<<"0\nENDSEC\n0\nEOF\n";out.close();
    auto profile=sketcher::Sketch::create_default();static_cast<void>(interchange::import_dxf(dir/"periodic.dxf",profile));
    auto part=document::PartDocument::create_default();auto extrusion=document::PartDocument::create_extrusion_container(profile.id);extrusion.extrusion.height=3;
    part.sketches.push_back(profile);part.history.push_back(extrusion);const auto bodies=kernel.evaluate_history(part.kernel_operations());
    require(bodies.size()==1&&!bodies.back().kernel_shape.empty(),"Periodic spline did not form a closed solid");near(bodies.back().volume,40);
}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.message);return result;
}
void command_cases(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","splines"}});const auto id=live.active_document_id();
    run(host,"box.create",{{"length_mm","3"},{"width_mm","4"},{"height_mm","5"}});const auto cached=live.open_part(id)->session.calculated_boundaries().back().kernel_shape;
    const auto count=live.open_part(id)->session.document().sketches.size();const auto result=run(host,"import.dxf",{{"path","splines.dxf"}}).data;const auto sketch=result.at("sketch").get<std::string>();
    require(result.at("imported_entities")==4&&result.at("body_calculated")==false&&live.open_part(id)->session.calculated_boundaries().back().kernel_shape==cached,"Spline import unexpectedly calculated a body");
    const auto current=[&]{return workspace::document_sketch(live,id,sketch);};test::check_unclamped_dxf_sketch(current());const auto saved=current().serialized();
    run(host,"undo");require(live.open_part(id)->session.document().sketches.size()==count,"Spline Undo retained import");run(host,"redo");require(current().serialized()==saved,"Spline Redo changed exact identities");
    run(host,"save");test::check_unclamped_dxf_sketch(document::PartDocument::load(dir/"splines.prtz").sketches.back());
    const auto revision=live.open_part(id)->session.revision();require(!host.execute({{"command","import.dxf"},{"arguments",{{"path","invalid.dxf"},{"sketch",sketch}}}}).ok,"Invalid spline command accepted");
    require(live.open_part(id)->session.revision()==revision&&current().serialized()==saved,"Invalid spline command changed history");
    run(host,"new",{{"type","assembly"},{"name","splines-assembly"}});const auto owner=live.active_document_id();const auto root_sketch=run(host,"sketch.create",{{"name","Splines"},{"plane","XY"}}).data.at("sketch").get<std::string>();
    run(host,"import.dxf",{{"path","splines.dxf"},{"sketch",root_sketch}});run(host,"save");test::check_unclamped_dxf_sketch(assembly::AssemblyDocument::load(dir/"splines-assembly.asmz").sketches.back());
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-dxf-spline-"+kernel::make_stable_id());fs::create_directory(dir);math_cases();import_cases(kernel,dir);command_cases(kernel,dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected test cleanup path");fs::remove_all(dir);std::cout<<"Unclamped DXF spline geometry, rational weights, closure, trim, native data, solid volume and history passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
