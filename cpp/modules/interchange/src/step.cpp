#include <zima/interchange/step.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace zima::interchange {
namespace {

TopoDS_Shape read_step(const std::filesystem::path& path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone ||
        reader.TransferRoots() == 0) {
        throw std::runtime_error("STEP soubor nelze načíst nebo neobsahuje geometrii");
    }
    const TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) throw std::runtime_error("STEP soubor neobsahuje tvar");
    return shape;
}

std::array<double, 2> project(const gp_Pnt& point, const gp_Ax3& frame) {
    const gp_Vec offset(frame.Location(), point);
    return {offset.Dot(gp_Vec(frame.XDirection())),
            offset.Dot(gp_Vec(frame.YDirection()))};
}

PlanarCurve extract_curve(const TopoDS_Edge& edge, const gp_Ax3& frame) {
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    if (!std::isfinite(first) || !std::isfinite(last) || last <= first) {
        throw std::runtime_error("STEP hrana má neplatný parametrický rozsah");
    }
    auto start = project(curve.Value(first), frame);
    auto end = project(curve.Value(last), frame);
    if (edge.Orientation() == TopAbs_REVERSED) std::swap(start, end);
    if (curve.GetType() == GeomAbs_Line) return PlanarLine{start, end};
    if (curve.GetType() == GeomAbs_Circle) {
        const auto circle = curve.Circle();
        const auto center = project(circle.Location(), frame);
        constexpr double full_turn = 2.0 * 3.14159265358979323846;
        if (std::abs((last - first) - full_turn) <= 1.0e-7) {
            return PlanarCircle{center, circle.Radius()};
        }
        return PlanarArc{center, start, end};
    }
    if (curve.GetType() == GeomAbs_Ellipse) {
        const auto ellipse = curve.Ellipse();
        constexpr double full_turn = 2.0 * 3.14159265358979323846;
        if (std::abs((last - first) - full_turn) > 1.0e-7) {
            throw std::runtime_error("Oříznutá elipsa STEP zatím není podporována");
        }
        const auto center = project(ellipse.Location(), frame);
        const gp_Pnt major = ellipse.Location().Translated(
            gp_Vec(ellipse.XAxis().Direction()) * ellipse.MajorRadius());
        const gp_Pnt minor = ellipse.Location().Translated(
            gp_Vec(ellipse.YAxis().Direction()) * ellipse.MinorRadius());
        return PlanarEllipse{center, project(major, frame), project(minor, frame)};
    }
    throw std::runtime_error("STEP plocha obsahuje nepodporovaný typ křivky");
}

}  // namespace

std::vector<StepPlanarFace> extract_step_planar_faces(
    const std::filesystem::path& path, std::size_t maximum_faces) {
    if (maximum_faces == 0) throw std::invalid_argument("Limit STEP ploch musí být kladný");
    const TopoDS_Shape shape = read_step(path);
    std::vector<StepPlanarFace> result;
    std::size_t face_index{};
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const auto face = TopoDS::Face(faces.Current());
        BRepAdaptor_Surface surface(face, true);
        const std::string key = "step-face:" + std::to_string(face_index++);
        if (surface.GetType() != GeomAbs_Plane) continue;
        PlanarFaceProfile profile{path.stem().string(), key, {}};
        const gp_Ax3 frame = surface.Plane().Position();
        try {
            for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
                profile.curves.push_back(extract_curve(TopoDS::Edge(edges.Current()), frame));
            }
        } catch (const std::exception&) {
            continue;
        }
        if (!profile.curves.empty()) {
            if (result.size() == maximum_faces) {
                throw std::runtime_error(
                    "STEP obsahuje příliš mnoho převoditelných rovinných ploch");
            }
            result.push_back({key, std::move(profile)});
        }
    }
    return result;
}

std::optional<StepPlanarFace> extract_step_planar_face(
    const std::filesystem::path& path, const std::string& face_key) {
    constexpr std::string_view prefix = "step-face:";
    if (!face_key.starts_with(prefix)) {
        throw std::invalid_argument("Neplatná identita STEP plochy");
    }
    std::size_t requested{};
    try { requested = std::stoull(face_key.substr(prefix.size())); }
    catch (const std::exception&) { throw std::invalid_argument("Neplatná identita STEP plochy"); }
    const TopoDS_Shape shape = read_step(path);
    std::size_t face_index{};
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next(), ++face_index) {
        if (face_index != requested) continue;
        const auto face = TopoDS::Face(faces.Current());
        BRepAdaptor_Surface surface(face, true);
        if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
        PlanarFaceProfile profile{path.stem().string(), face_key, {}};
        const gp_Ax3 frame = surface.Plane().Position();
        for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
            profile.curves.push_back(extract_curve(TopoDS::Edge(edges.Current()), frame));
        }
        if (profile.curves.empty()) return std::nullopt;
        return StepPlanarFace{face_key, std::move(profile)};
    }
    return std::nullopt;
}

}  // namespace zima::interchange
