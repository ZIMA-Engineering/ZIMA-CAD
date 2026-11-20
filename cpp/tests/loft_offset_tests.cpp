#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
namespace k = zima::kernel;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Independent of the user's document: all three authored profiles are convex.
// Their smooth Loft has longitudinal junctions whose convexity changes.
k::LoftRequest mixed_profiles() {
    k::LoftRequest request;
    request.tolerance = .001;
    request.region_id = "mixed-profiles";
    request.match_profile_vertices = true;
    request.make_solid = true;
    using E = k::ExtrusionRequest;
    for (unsigned i = 0; i < 3; ++i) {
        E profile;
        profile.direction = {0, 0, 1};
        profile.outer_boundary_id = "contour";
        profile.outer_edge_source_ids = {"bottom", "right", "top", "left"};
        profile.outer_vertex_source_ids = {"a", "b", "c", "d"};
        if (i == 0) profile.outer_profile = E::PolygonProfile{{
            {-90, -35, 0}, {90, -35, 0}, {90, 35, 0}, {-90, 35, 0}}};
        if (i == 1) {
            const double x = std::sqrt(55. * 55. - 35. * 35.);
            profile.outer_profile = E::CurvedProfile{{
                E::ArcCurve{{-x,-35,65}, {0,-55,65}, {x,-35,65}},
                E::ArcCurve{{x,-35,65}, {55,0,65}, {x,35,65}},
                E::ArcCurve{{x,35,65}, {0,55,65}, {-x,35,65}},
                E::ArcCurve{{-x,35,65}, {-55,0,65}, {-x,-35,65}}}};
        }
        if (i == 2) profile.outer_profile = E::CurvedProfile{{
            E::LineCurve{{-65,-12,153}, {65,-12,153}},
            E::ArcCurve{{65,-12,153}, {77,0,153}, {65,12,153}},
            E::LineCurve{{65,12,153}, {-65,12,153}},
            E::ArcCurve{{-65,12,153}, {-77,0,153}, {-65,-12,153}}}};
        request.profiles.push_back(std::move(profile));
        request.profile_owners.push_back("profile-" + std::to_string(i));
    }
    return request;
}

double valid_volume(const k::BodyResult& body) {
    if (!body.calculation_errors.empty())
        throw std::runtime_error(body.calculation_errors.begin()->second);
    const auto shape = k::read_calculated_body_shape(body);
    require(!shape.IsNull(), "Missing calculated shape");
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> solids;
    TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    require(solids.Extent() == 1, "Offset must produce one connected solid");
    require(BRepCheck_Analyzer(shape, true, false, true).IsValid(),
        "Invalid offset solid");
    require(BRepAlgoAPI_Check(shape, false, true).IsValid(),
        "Self-interfering offset solid");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    require(std::isfinite(properties.Mass()) && properties.Mass() > 0.,
        "Offset solid has no positive volume");
    return properties.Mass();
}

void check_normal_offsets(const TopoDS_Shape& carrier,
    const k::BodyResult& body, const std::vector<double>& offsets) {
    // Distance to a compound of boundary faces cannot pass merely because
    // the queried point lies inside an incorrectly returned full solid.
    BRep_Builder builder;
    TopoDS_Compound boundary;
    builder.MakeCompound(boundary);
    const auto shape = k::read_calculated_body_shape(body);
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next())
        builder.Add(boundary, f.Current());
    for (TopExp_Explorer f(carrier, TopAbs_FACE); f.More(); f.Next()) {
        const auto face = TopoDS::Face(f.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) continue;
        for (double u : {.25, .5, .75}) for (double v : {.2, .6, .8}) {
            gp_Pnt point;
            gp_Vec du, dv;
            surface.D1(surface.FirstUParameter() + u *
                (surface.LastUParameter() - surface.FirstUParameter()),
                surface.FirstVParameter() + v *
                (surface.LastVParameter() - surface.FirstVParameter()),
                point, du, dv);
            auto normal = du.Crossed(dv).Normalized();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            for (double offset : offsets) {
                const auto expected = point.Translated(normal * offset);
                BRepExtrema_DistShapeShape distance(
                    BRepBuilderAPI_MakeVertex(expected).Vertex(), boundary);
                require(distance.IsDone() && distance.Value() < 1e-6,
                    "Wall does not preserve the requested normal offset");
            }
        }
    }
}
}

int main(int argc, char** argv) { try {
    const bool shell_mode = argc == 2 && std::string_view(argv[1]) == "--shell";
    require(shell_mode || (argc == 2 && std::string_view(argv[1]) == "--thin"),
        "Expected --shell or --thin");
    k::OcctKernel kernel;
    const auto request = mixed_profiles();
    const auto carrier = kernel.evaluate_history({{"loft", request}}).back();
    const double carrier_volume = valid_volume(carrier);
    const auto carrier_shape = k::read_calculated_body_shape(carrier);
    unsigned failures = 0, cases = 1;
    auto straight = request;
    for (unsigned i = 0; i < 3; ++i) {
        const double z = i == 0 ? 0. : i == 1 ? 65. : 153.;
        straight.profiles[i].outer_profile = k::ExtrusionRequest::PolygonProfile{{
            {-90,-35,z}, {90,-35,z}, {90,35,z}, {-90,35,z}}};
    }
    try {
    k::BodyResult straight_wall;
    if (shell_mode) {
        k::ShellRequest shell;
        shell.thickness = 1.;
        shell.removed_faces = {{"loft", "loft:cap:from:profile-0", {}},
            {"loft", "loft:cap:from:profile-2", {}}};
        straight_wall = kernel.evaluate_history({{"loft", straight}, {"shell", shell}}).back();
    } else {
        straight.thin = true;
        straight.thickness = 1.;
        straight.thickness_side = k::SurfaceThicknessSide::Second;
        straight_wall = kernel.evaluate_history({{"loft", straight}}).back();
    }
    require(std::abs(valid_volume(straight_wall) - (180.*70. - 178.*68.)*153.) < 1e-5,
        "Straight rectangular control lost its independently calculated wall volume");
    std::cout << "Straight rectangular control passed" << std::endl;
    } catch (const std::exception& error) {
        ++failures;
        std::cout << "Straight rectangular control FAILED: " << error.what() << std::endl;
    }
    for (double thickness : {.1, 1., 3.}) {
        for (unsigned variant = 0; variant < (shell_mode ? 4u : 3u); ++variant) {
            ++cases;
            std::cout << (shell_mode ? "Shell" : "Thin Loft")
                << " thickness=" << thickness << " variant=" << variant << ": "
                << std::flush;
            try {
                k::BodyResult body;
                std::vector<double> offsets;
                if (shell_mode) {
                    k::ShellRequest shell;
                    shell.thickness = thickness;
                    for (unsigned end = 0; end < 2; ++end)
                        if (variant & (1u << end)) shell.removed_faces.push_back({
                            "loft", "loft:cap:from:profile-" +
                                std::to_string(end ? 2 : 0), {}});
                    body = kernel.evaluate_history({{"loft", request},
                        {"shell", shell}}).back();
                    offsets = {0., -thickness};
                } else {
                    auto thin = request;
                    thin.thin = true;
                    thin.thickness = thickness;
                    thin.thickness_side = static_cast<k::SurfaceThicknessSide>(variant);
                    body = kernel.evaluate_history({{"loft", thin}}).back();
                    offsets = variant == 2 ? std::vector<double>{-thickness*.5, thickness*.5}
                        : std::vector<double>{0., variant == 1 ? -thickness : thickness};
                }
                const double volume = valid_volume(body);
                require(volume < carrier_volume * .5,
                    "Offset returned full carrier material instead of a wall");
                check_normal_offsets(carrier_shape, body, offsets);
                std::cout << "passed, volume=" << volume << '\n';
            } catch (const std::exception& error) {
                ++failures;
                std::cout << "FAILED: " << error.what() << '\n';
            }
        }
    }
    std::cout << "Failures=" << failures << '/' << cases << std::endl;
    return failures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
}}
