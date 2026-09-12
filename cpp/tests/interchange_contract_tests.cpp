#include "dxf_export_test_support.hpp"
#include <iomanip>
#include <set>
#include <zima/kernel/stable_id.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/planar_face.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void verify_exact_dxf_import() {
    using namespace zima;using namespace interchange;namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-dxf-curves-"+kernel::make_stable_id());fs::create_directory(dir);
    auto source=test::dxf_curve_fixture();export_dxf(dir/"source.dxf",source);
    auto target=sketcher::Sketch::create_default();target.plane=sketcher::SketchPlane::YZ;const auto sketch_id=target.id;
    const auto report=import_dxf(dir/"source.dxf",target,10);
    require(report.imported_entities==11&&report.source_entities==12&&report.warnings.size()==1&&report.warnings.front().find("POINT")!=std::string::npos,"DXF exact-curve import lost types or its explicit POINT warning");
    require(target.id==sketch_id&&target.plane==sketcher::SketchPlane::YZ&&target.import_blocks.size()==1,"DXF import replaced its destination or lost the native block");
    export_dxf(dir/"roundtrip.dxf",target);test::check_dxf_curves(dir/"roundtrip.dxf",false);
    std::ifstream input(dir/"source.dxf");std::string unitless{std::istreambuf_iterator<char>(input),{}};input.close();const std::string units="$INSUNITS\n70\n4\n";
    const auto unit_position=unitless.find(units);require(unit_position!=std::string::npos,"DXF units header missing");unitless.replace(unit_position,units.size(),"$INSUNITS\n70\n0\n");
    std::ofstream(dir/"unitless.dxf")<<unitless;auto scaled=sketcher::Sketch::create_default();static_cast<void>(import_dxf(dir/"unitless.dxf",scaled,10));
    require(std::abs(scaled.ellipses.front().major_radius-50)<1e-9&&std::abs(scaled.find_point(scaled.bsplines.front().control_point_ids.front())->x-1000)<1e-9&&scaled.bsplines.front().knots==target.bsplines.front().knots&&scaled.bsplines.front().weights==target.bsplines.front().weights,"DXF scaling changed dimensionless spline data or missed curve coordinates");
    const auto restored=sketcher::Sketch::from_serialized(target.serialized());export_dxf(dir/"restored.dxf",restored);test::check_dxf_curves(dir/"restored.dxf",false);
    const auto first_ids=target.import_blocks.front().point_ids;const auto first_points=target.points;
    const auto second=import_dxf(dir/"source.dxf",target);const std::set<std::string> first(first_ids.begin(),first_ids.end());
    for(const auto& id:target.import_blocks.back().point_ids)require(!first.contains(id),"Repeated DXF import borrowed points from its older block");
    target.transform_import_block(second.import_block_id,10,-2,0);
    for(const auto& point:first_points)require(*target.find_point(point.id)==point,"Moving one DXF block moved another block");
    // Circle/arc native factories also must not reuse old coincident points.
    auto circular=sketcher::Sketch::create_default();static_cast<void>(circular.add_circle(0,0,5));static_cast<void>(circular.add_arc(10,0,15,0,10,5));export_dxf(dir/"circular.dxf",circular);
    auto repeated=sketcher::Sketch::create_default();static_cast<void>(import_dxf(dir/"circular.dxf",repeated));const auto originals=repeated.points;
    const auto repeat=import_dxf(dir/"circular.dxf",repeated);repeated.transform_import_block(repeat.import_block_id,7,9,0);
    for(const auto& point:originals)require(*repeated.find_point(point.id)==point,"Circular import blocks share editable center/end points");
    const auto before=target.serialized();
    for(int failure=0;failure<5;++failure) {
        const auto file=dir/"invalid.dxf";std::ofstream out(file);out<<std::setprecision(17);
        out<<"0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n0\nSPLINE\n70\n"<<(failure==4?9:12)<<"\n71\n2\n72\n6\n73\n"<<(failure==3?4:3)<<'\n';
        const std::array<double,6> knots=failure==1?std::array<double,6>{0,1,2,3,4,5}:std::array<double,6>{0,0,0,1,1,1};
        for(double knot:knots)out<<"40\n"<<knot<<'\n';
        out<<"41\n1\n41\n"<<(failure==2?-1:std::sqrt(.5))<<"\n41\n1\n10\n1\n20\n0\n30\n0\n10\n1\n20\n1\n30\n"<<(failure==0?1:0)<<"\n10\n0\n20\n1\n30\n0\n0\nENDSEC\n0\nEOF\n";out.close();
        bool rejected=false;try{static_cast<void>(import_dxf(file,target));}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&target.serialized()==before,"Invalid DXF spline partly committed a preceding valid entity");
    }
    require(dir.parent_path()==parent,"Unsafe test cleanup");fs::remove_all(dir);
}
}

int main() {
    using namespace zima::interchange;
    try {
        require(format_from_path("profile.DXF") == Format::Dxf &&
                    format_from_path("model.stp") == Format::Step &&
                    format_from_path("view.JPEG") == Format::Jpeg,
                "Interchange extension dispatch is not case insensitive");
        require(supports(Format::Dxf, Direction::Import, Context::Sketch) &&
                    supports(Format::Dxf, Direction::Import, Context::Part) &&
                    !supports(Format::Step, Direction::Import, Context::Sketch) &&
                    supports(Format::Step, Direction::Import, Context::Assembly),
                "Import context contract is invalid");
        require(supports(Format::Dxf, Direction::Export, Context::Sketch) &&
                    !supports(Format::Dxf, Direction::Export, Context::Part) &&
                    supports(Format::Stl, Direction::Export, Context::Part) &&
                    supports(Format::Png, Direction::Export, Context::Assembly),
                "Export context contract is invalid");
        auto source = zima::sketcher::Sketch::create_default();
        const auto segment = source.add_segment(0.0, 0.0, 20.0, 0.0);
        source.segments.back().construction = true;
        static_cast<void>(source.add_circle(5.0, 8.0, 3.0));
        static_cast<void>(source.add_arc(0.0, 0.0, 5.0, 0.0, 0.0, 5.0));
        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-dxf-roundtrip.dxf";
        export_dxf(path, source);
        auto imported = zima::sketcher::Sketch::create_default();
        const auto result = import_dxf(path, imported);
        std::filesystem::remove(path);
        require(result.source_entities == 3 && result.imported_entities == 3 &&
                    !result.import_block_id.empty() &&
                    imported.import_blocks.size() == 1 &&
                    imported.segments.size() == 1 && imported.circles.size() == 1 &&
                    imported.arcs.size() == 1 && imported.segments.front().construction,
                "DXF geometry did not import as one editable ZIMA block");
        const auto before = imported.points.front();
        imported.transform_import_block(result.import_block_id, 10.0, -2.0, 0.0);
        require(imported.points.front().x == before.x + 10.0 &&
                    imported.points.front().y == before.y - 2.0,
                "DXF block translation did not preserve editable entities");
        const auto restored = zima::sketcher::Sketch::from_serialized(
            imported.serialized());
        require(restored.import_blocks == imported.import_blocks &&
                    restored.segments.front().id == imported.segments.front().id &&
                    segment != restored.segments.front().id,
                "DXF block or stable imported identities did not survive save/load");
        auto dense = zima::sketcher::Sketch::create_default();
        for (int index = 0; index < 101; ++index) {
            static_cast<void>(dense.add_segment(
                static_cast<double>(index), 0.0,
                static_cast<double>(index), 1.0));
        }
        export_dxf(path, dense);
        auto rejected_target = zima::sketcher::Sketch::create_default();
        bool limit_rejected = false;
        try {
            static_cast<void>(import_dxf(path, rejected_target, 1.0, 100));
        } catch (const std::runtime_error&) {
            limit_rejected = true;
        }
        std::filesystem::remove(path);
        require(limit_rejected && rejected_target.points.empty() &&
                    rejected_target.segments.empty() &&
                    rejected_target.import_blocks.empty(),
                "Oversized DXF was not rejected before mutating the Sketch");
        auto face_sketch = zima::sketcher::Sketch::create_default();
        const auto face_block = append_planar_face_as_sketch_block({
            "STEP profil", "face:front", {
                PlanarLine{{0.0, 0.0}, {20.0, 0.0}},
                PlanarArc{{20.0, 5.0}, {20.0, 0.0}, {25.0, 5.0}},
                PlanarCircle{{10.0, 10.0}, 2.0},
            }}, face_sketch);
        require(!face_block.empty() && face_sketch.import_blocks.size() == 1 &&
                    face_sketch.segments.size() == 1 && face_sketch.arcs.size() == 1 &&
                    face_sketch.circles.size() == 1 &&
                    face_sketch.import_blocks.front().source_path == "face:front",
                "Analytic STEP face profile did not become one normal Sketch block");
        verify_exact_dxf_import();
        std::cout << "C++ interchange contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
