#include <zima/interchange/step.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <TDF_Tool.hxx>
#include <gp_Quaternion.hxx>
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

std::vector<StepPart> inspect_step_parts(
    const std::filesystem::path& path, std::size_t maximum_parts) {
    if (maximum_parts == 0) throw std::invalid_argument("Limit STEP dílů musí být kladný");
    Handle(TDocStd_Document) document;
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", document);
    STEPCAFControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone ||
        !reader.Transfer(document)) {
        throw std::runtime_error("STEP produktovou strukturu nelze načíst");
    }
    std::vector<StepPart> result;
    XCAFPrs_DocumentExplorer explorer(
        document, XCAFPrs_DocumentExplorerFlags_NoStyle);
    for (; explorer.More(); explorer.Next()) {
        if (result.size() == maximum_parts) {
            throw std::runtime_error("STEP obsahuje příliš mnoho dílů");
        }
        const auto& node = explorer.Current();
        std::string name = "STEP díl " + std::to_string(result.size() + 1);
        Handle(TDataStd_Name) attribute;
        const TDF_Label& name_label = node.RefLabel.IsNull() ? node.Label : node.RefLabel;
        if (name_label.FindAttribute(TDataStd_Name::GetID(), attribute)) {
            const auto& extended = attribute->Get();
            std::vector<char> utf8(static_cast<std::size_t>(extended.LengthOfCString()) + 1);
            Standard_PCharacter buffer = utf8.data();
            extended.ToUTF8CString(buffer);
            if (utf8.front() != '\0') name = utf8.data();
        }
        TCollection_AsciiString definition;
        TDF_Tool::Entry(node.RefLabel.IsNull() ? node.Label : node.RefLabel, definition);
        std::string parent_path;
        const auto depth = explorer.CurrentDepth();
        if (depth > 0) parent_path = explorer.Current(depth - 1).Id.ToCString();
        const gp_Trsf transform = node.LocalTrsf.Transformation();
        double rotation_x{};
        double rotation_y{};
        double rotation_z{};
        transform.GetRotation().GetEulerAngles(
            gp_Intrinsic_XYZ, rotation_x, rotation_y, rotation_z);
        constexpr double degrees = 180.0 / 3.14159265358979323846;
        const auto translation = transform.TranslationPart();
        const gp_Trsf global_transform = node.Location.Transformation();
        double global_rx{};
        double global_ry{};
        double global_rz{};
        global_transform.GetRotation().GetEulerAngles(
            gp_Intrinsic_XYZ, global_rx, global_ry, global_rz);
        const auto global_translation = global_transform.TranslationPart();
        result.push_back({node.Id.ToCString(), std::move(parent_path),
            definition.ToCString(), std::move(name), node.IsAssembly,
            translation.X(), translation.Y(), translation.Z(),
            rotation_x * degrees, rotation_y * degrees, rotation_z * degrees,
            global_translation.X(), global_translation.Y(), global_translation.Z(),
            global_rx * degrees, global_ry * degrees, global_rz * degrees});
    }
    return result;
}

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
