#include <zima/kernel/occt_kernel.hpp>

#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDF_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <array>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <vector>

namespace zima::kernel {
namespace {

void validate_box(const BoxRequest& request) {
    if (!std::isfinite(request.length) || !std::isfinite(request.width) ||
        !std::isfinite(request.height) || request.length <= 0.0 ||
        request.width <= 0.0 || request.height <= 0.0) {
        throw std::invalid_argument("Box dimensions must be finite and positive");
    }
    for (const double value : {
            request.translation.x, request.translation.y, request.translation.z,
            request.rotation_degrees.x, request.rotation_degrees.y,
            request.rotation_degrees.z}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Box placement must be finite");
        }
    }
}

template <typename Reference>
struct OwnedTopology {
    TopoDS_Shape shape;
    Reference reference;
};

using OwnedFace = OwnedTopology<FaceReference>;
using OwnedEdge = OwnedTopology<EdgeReference>;
using OwnedVertex = OwnedTopology<VertexReference>;

template <typename Algorithm, typename Owned>
std::vector<Owned> propagate_topology(
    Algorithm& algorithm, const std::vector<Owned>& existing,
    const std::vector<Owned>& operand);

struct PrimitiveData {
    TopoDS_Shape shape;
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
};

gp_Trsf primitive_transform(const Vec3& translation, const Vec3& rotation_degrees) {
    constexpr double radians_per_degree = std::numbers::pi / 180.0;
    gp_Trsf transform;
    transform.SetTranslation(gp_Vec(translation.x, translation.y, translation.z));
    for (const auto& rotation : std::array<std::pair<gp_Dir, double>, 3>{
            std::pair{gp_Dir(0.0, 0.0, 1.0), rotation_degrees.z},
            std::pair{gp_Dir(0.0, 1.0, 0.0), rotation_degrees.y},
            std::pair{gp_Dir(1.0, 0.0, 0.0), rotation_degrees.x}}) {
        gp_Trsf next;
        next.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), rotation.first),
                         rotation.second * radians_per_degree);
        transform.Multiply(next);
    }
    return transform;
}

ViewerAxis transformed_axis(
    const std::string& owner_id,
    const std::string& key,
    const Vec3& local_direction,
    double display_length,
    const gp_Trsf& transform) {
    gp_Pnt point(0.0, 0.0, 0.0);
    point.Transform(transform);
    gp_Vec direction(local_direction.x, local_direction.y, local_direction.z);
    direction.Transform(transform);
    direction.Normalize();
    return {{point.X(), point.Y(), point.Z()},
            {direction.X(), direction.Y(), direction.Z()}, display_length,
            {owner_id, key}};
}

std::vector<ViewerAxis> axes_for_operation(const HistoryOperation& operation) {
    return std::visit([&](const auto& primitive) {
        using Request = std::decay_t<decltype(primitive)>;
        if constexpr (std::is_same_v<Request, BoxRequest>) {
            const auto transform = primitive_transform(
                primitive.translation, primitive.rotation_degrees);
            const double length = std::max({
                primitive.length, primitive.width, primitive.height});
            return std::vector<ViewerAxis>{
                transformed_axis(operation.owner_id, "axis:x", {1.0, 0.0, 0.0},
                                 length, transform),
                transformed_axis(operation.owner_id, "axis:y", {0.0, 1.0, 0.0},
                                 length, transform),
                transformed_axis(operation.owner_id, "axis:z", {0.0, 0.0, 1.0},
                                 length, transform)};
        } else if constexpr (std::is_same_v<Request, CylinderRequest>) {
            const auto transform = primitive_transform(
                primitive.translation, primitive.rotation_degrees);
            const double length = std::max(primitive.height, primitive.radius * 2.0);
            return std::vector<ViewerAxis>{
                transformed_axis(operation.owner_id, "axis", {0.0, 0.0, 1.0},
                                 length, transform)};
        } else if constexpr (std::is_same_v<Request, SphereRequest>) {
            const auto transform = primitive_transform(
                primitive.translation, primitive.rotation_degrees);
            return std::vector<ViewerAxis>{
                transformed_axis(operation.owner_id, "axis:x", {1.0, 0.0, 0.0},
                                 primitive.radius * 2.0, transform),
                transformed_axis(operation.owner_id, "axis:y", {0.0, 1.0, 0.0},
                                 primitive.radius * 2.0, transform),
                transformed_axis(operation.owner_id, "axis:z", {0.0, 0.0, 1.0},
                                 primitive.radius * 2.0, transform)};
        } else if constexpr (std::is_same_v<Request, ConeRequest>) {
            const auto transform = primitive_transform(
                primitive.translation, primitive.rotation_degrees);
            return std::vector<ViewerAxis>{transformed_axis(operation.owner_id,
                "axis", {0.0, 0.0, 1.0},
                std::max({primitive.height, primitive.bottom_radius * 2.0,
                          primitive.top_radius * 2.0}), transform)};
        } else if constexpr (std::is_same_v<Request, PyramidRequest> ||
                             std::is_same_v<Request, WedgeRequest>) {
            const auto transform = primitive_transform(
                primitive.translation, primitive.rotation_degrees);
            const double length = std::max({primitive.length, primitive.width,
                                            primitive.height});
            return std::vector<ViewerAxis>{
                transformed_axis(operation.owner_id, "axis:x", {1.0, 0.0, 0.0}, length, transform),
                transformed_axis(operation.owner_id, "axis:y", {0.0, 1.0, 0.0}, length, transform),
                transformed_axis(operation.owner_id, "axis:z", {0.0, 0.0, 1.0}, length, transform)};
        } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
            const double length = std::sqrt(
                primitive.direction.x * primitive.direction.x +
                primitive.direction.y * primitive.direction.y +
                primitive.direction.z * primitive.direction.z);
            const Vec3 origin = std::visit([](const auto& profile) {
                using Profile = std::decay_t<decltype(profile)>;
                if constexpr (std::is_same_v<Profile,
                                  ExtrusionRequest::PolygonProfile>) {
                    return profile.vertices.front();
                } else if constexpr (std::is_same_v<Profile,
                                         ExtrusionRequest::CircleProfile>) {
                    return profile.center;
                } else if constexpr (std::is_same_v<Profile,
                                         ExtrusionRequest::EllipseProfile>) {
                    return profile.center;
                } else {
                    return std::visit([](const auto& curve) {
                        return curve.start;
                    }, profile.curves.front());
                }
            }, primitive.outer_profile);
            return std::vector<ViewerAxis>{{
                origin,
                {primitive.direction.x / length, primitive.direction.y / length,
                 primitive.direction.z / length},
                length, {operation.owner_id, "axis"}}};
        } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
            const double length = std::max(10.0, std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z));
            const double direction_length = std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z);
            return std::vector<ViewerAxis>{{
                primitive.axis_point,
                {primitive.axis_direction.x / direction_length,
                 primitive.axis_direction.y / direction_length,
                 primitive.axis_direction.z / direction_length},
                length, {operation.owner_id, "axis"}}};
        } else {
            return std::vector<ViewerAxis>{};
        }
    }, operation.primitive);
}

std::string axis_position(double value, double maximum, const char* axis) {
    return std::string(axis) + (std::abs(value) <= std::abs(value - maximum)
        ? "_min" : "_max");
}

std::string semantic_box_vertex(
    const TopoDS_Vertex& vertex, const BoxRequest& request) {
    const gp_Pnt point = BRep_Tool::Pnt(vertex);
    return axis_position(point.X(), request.length, "x") + ":" +
        axis_position(point.Y(), request.width, "y") + ":" +
        axis_position(point.Z(), request.height, "z");
}

std::string semantic_box_edge(
    const TopoDS_Edge& edge, const BoxRequest& request) {
    TopoDS_Vertex first;
    TopoDS_Vertex last;
    TopExp::Vertices(edge, first, last);
    std::string first_key = semantic_box_vertex(first, request);
    std::string last_key = semantic_box_vertex(last, request);
    if (last_key < first_key) std::swap(first_key, last_key);
    return "edge:" + first_key + "--" + last_key;
}

std::string semantic_box_face(
    const TopoDS_Face& face, const BoxRequest& request) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    const gp_Pnt center = properties.CentreOfMass();
    const std::array<std::pair<double, const char*>, 6> distances{{
        {std::abs(center.X()), "x_min"},
        {std::abs(center.X() - request.length), "x_max"},
        {std::abs(center.Y()), "y_min"},
        {std::abs(center.Y() - request.width), "y_max"},
        {std::abs(center.Z()), "z_min"},
        {std::abs(center.Z() - request.height), "z_max"},
    }};
    return std::min_element(distances.begin(), distances.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        })->second;
}

PrimitiveData make_box_data(const BoxRequest& request, const std::string& owner_id) {
    const TopoDS_Shape unplaced = BRepPrimAPI_MakeBox(
        request.length, request.width, request.height).Shape();
    const gp_Trsf transform = primitive_transform(
        request.translation, request.rotation_degrees);
    BRepBuilderAPI_Transform transformer(unplaced, transform, true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face original = TopoDS::Face(explorer.Current());
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull() && transformed.ShapeType() == TopAbs_FACE) {
            result.faces.push_back({
                transformed,
                {owner_id, semantic_box_face(original, request)},
            });
        }
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge original = TopoDS::Edge(explorer.Current());
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull() && transformed.ShapeType() == TopAbs_EDGE) {
            result.edges.push_back({
                transformed, {owner_id, semantic_box_edge(original, request)}});
        }
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const TopoDS_Vertex original = TopoDS::Vertex(explorer.Current());
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull() && transformed.ShapeType() == TopAbs_VERTEX) {
            result.vertices.push_back({
                transformed, {owner_id, semantic_box_vertex(original, request)}});
        }
    }
    return result;
}

void validate_cylinder(const CylinderRequest& request) {
    if (!std::isfinite(request.radius) || !std::isfinite(request.height) ||
        request.radius <= 0.0 || request.height <= 0.0) {
        throw std::invalid_argument("Cylinder dimensions must be finite and positive");
    }
    for (const double value : {
            request.translation.x, request.translation.y, request.translation.z,
            request.rotation_degrees.x, request.rotation_degrees.y,
            request.rotation_degrees.z}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Cylinder placement must be finite");
        }
    }
}

PrimitiveData make_cylinder_data(
    const CylinderRequest& request, const std::string& owner_id) {
    const TopoDS_Shape unplaced =
        BRepPrimAPI_MakeCylinder(request.radius, request.height).Shape();
    const gp_Trsf transform = primitive_transform(
        request.translation, request.rotation_degrees);
    BRepBuilderAPI_Transform transformer(unplaced, transform, true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face original = TopoDS::Face(explorer.Current());
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(original, properties);
        const double z = properties.CentreOfMass().Z();
        const std::string key = std::abs(z) < 1.0e-7 ? "z_min"
            : std::abs(z - request.height) < 1.0e-7 ? "z_max" : "side";
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull()) result.faces.push_back({transformed, {owner_id, key}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge original = TopoDS::Edge(explorer.Current());
        TopoDS_Vertex first;
        TopoDS_Vertex last;
        TopExp::Vertices(original, first, last);
        const double first_z = BRep_Tool::Pnt(first).Z();
        const double last_z = BRep_Tool::Pnt(last).Z();
        const std::string key = std::abs(first_z - last_z) > 1.0e-7
            ? "seam" : std::abs(first_z) < 1.0e-7
                ? "circle:z_min" : "circle:z_max";
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull()) result.edges.push_back({transformed, {owner_id, key}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const TopoDS_Vertex original = TopoDS::Vertex(explorer.Current());
        const std::string key = std::abs(BRep_Tool::Pnt(original).Z()) < 1.0e-7
            ? "seam:z_min" : "seam:z_max";
        const TopoDS_Shape transformed = transformer.ModifiedShape(original);
        if (!transformed.IsNull()) result.vertices.push_back({transformed, {owner_id, key}});
    }
    return result;
}

void validate_sphere(const SphereRequest& request) {
    if (!std::isfinite(request.radius) || request.radius <= 0.0) {
        throw std::invalid_argument("Sphere radius must be finite and positive");
    }
    for (const double value : {request.translation.x, request.translation.y,
            request.translation.z, request.rotation_degrees.x,
            request.rotation_degrees.y, request.rotation_degrees.z}) {
        if (!std::isfinite(value)) throw std::invalid_argument("Sphere placement must be finite");
    }
}

PrimitiveData make_sphere_data(
    const SphereRequest& request, const std::string& owner_id) {
    const TopoDS_Shape unplaced = BRepPrimAPI_MakeSphere(request.radius).Shape();
    BRepBuilderAPI_Transform transformer(unplaced,
        primitive_transform(request.translation, request.rotation_degrees), true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    std::size_t edge_index{};
    std::size_t vertex_index{};
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.faces.push_back({transformed, {owner_id, "surface"}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.edges.push_back({transformed,
            {owner_id, "edge:" + std::to_string(edge_index++)}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.vertices.push_back({transformed,
            {owner_id, "point:" + std::to_string(vertex_index++)}});
    }
    return result;
}

void validate_cone(const ConeRequest& request) {
    if (!std::isfinite(request.bottom_radius) ||
        !std::isfinite(request.top_radius) || !std::isfinite(request.height) ||
        request.bottom_radius < 0.0 || request.top_radius < 0.0 ||
        (request.bottom_radius <= 0.0 && request.top_radius <= 0.0) ||
        request.height <= 0.0) {
        throw std::invalid_argument("Cone dimensions are invalid");
    }
    for (const double value : {request.translation.x, request.translation.y,
            request.translation.z, request.rotation_degrees.x,
            request.rotation_degrees.y, request.rotation_degrees.z}) {
        if (!std::isfinite(value)) throw std::invalid_argument("Cone placement must be finite");
    }
}

PrimitiveData make_cone_data(const ConeRequest& request, const std::string& owner_id) {
    const TopoDS_Shape unplaced = BRepPrimAPI_MakeCone(
        request.bottom_radius, request.top_radius, request.height).Shape();
    BRepBuilderAPI_Transform transformer(unplaced,
        primitive_transform(request.translation, request.rotation_degrees), true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    std::size_t edge_index{};
    std::size_t vertex_index{};
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto face = TopoDS::Face(explorer.Current());
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        const double z = properties.CentreOfMass().Z();
        const std::string key = std::abs(z) < 1.0e-7 ? "z_min"
            : std::abs(z - request.height) < 1.0e-7 ? "z_max" : "side";
        const auto transformed = transformer.ModifiedShape(face);
        if (!transformed.IsNull()) result.faces.push_back({transformed, {owner_id, key}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.edges.push_back({transformed,
            {owner_id, "edge:" + std::to_string(edge_index++)}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.vertices.push_back({transformed,
            {owner_id, "point:" + std::to_string(vertex_index++)}});
    }
    return result;
}

PrimitiveData make_polyhedral_data(
    const TopoDS_Shape& unplaced, const gp_Trsf& transform,
    const std::string& owner_id) {
    BRepBuilderAPI_Transform transformer(unplaced, transform, true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    std::size_t face_index{};
    std::size_t edge_index{};
    std::size_t vertex_index{};
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.faces.push_back({transformed,
            {owner_id, "face:" + std::to_string(face_index++)}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.edges.push_back({transformed,
            {owner_id, "edge:" + std::to_string(edge_index++)}});
    }
    for (TopExp_Explorer explorer(unplaced, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.vertices.push_back({transformed,
            {owner_id, "point:" + std::to_string(vertex_index++)}});
    }
    return result;
}

void validate_pyramid(const PyramidRequest& request) {
    for (const double value : {request.length, request.width, request.height}) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::invalid_argument("Pyramid dimensions must be positive");
        }
    }
    for (const double value : {request.translation.x, request.translation.y,
            request.translation.z, request.rotation_degrees.x,
            request.rotation_degrees.y, request.rotation_degrees.z}) {
        if (!std::isfinite(value)) throw std::invalid_argument("Pyramid placement must be finite");
    }
}

PrimitiveData make_pyramid_data(
    const PyramidRequest& request, const std::string& owner_id) {
    BRepBuilderAPI_MakePolygon base;
    base.Add(gp_Pnt(-request.length / 2.0, -request.width / 2.0, 0.0));
    base.Add(gp_Pnt( request.length / 2.0, -request.width / 2.0, 0.0));
    base.Add(gp_Pnt( request.length / 2.0,  request.width / 2.0, 0.0));
    base.Add(gp_Pnt(-request.length / 2.0,  request.width / 2.0, 0.0));
    base.Close();
    BRepOffsetAPI_ThruSections builder(true, true);
    builder.AddWire(base.Wire());
    builder.AddVertex(BRepBuilderAPI_MakeVertex(
        gp_Pnt(0.0, 0.0, request.height)).Vertex());
    builder.Build();
    if (!builder.IsDone()) throw std::runtime_error("Pyramid calculation failed");
    return make_polyhedral_data(builder.Shape(),
        primitive_transform(request.translation, request.rotation_degrees), owner_id);
}

void validate_wedge(const WedgeRequest& request) {
    for (const double value : {request.length, request.width, request.height}) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::invalid_argument("Wedge dimensions must be positive");
        }
    }
    if (!std::isfinite(request.top_offset) || request.top_offset < 0.0 ||
        request.top_offset > request.length) {
        throw std::invalid_argument("Wedge top offset is outside its length");
    }
    for (const double value : {request.translation.x, request.translation.y,
            request.translation.z, request.rotation_degrees.x,
            request.rotation_degrees.y, request.rotation_degrees.z}) {
        if (!std::isfinite(value)) throw std::invalid_argument("Wedge placement must be finite");
    }
}

PrimitiveData make_wedge_data(const WedgeRequest& request, const std::string& owner_id) {
    const auto unplaced = BRepPrimAPI_MakeWedge(request.length, request.width,
        request.height, request.top_offset).Shape();
    gp_Trsf centered;
    centered.SetTranslation(gp_Vec(-request.length / 2.0, -request.width / 2.0, 0.0));
    BRepBuilderAPI_Transform centerer(unplaced, centered, true);
    return make_polyhedral_data(centerer.Shape(),
        primitive_transform(request.translation, request.rotation_degrees), owner_id);
}

void validate_extrusion(const ExtrusionRequest& request) {
    const auto validate_profile = [&](const auto& profile_variant) {
        std::visit([&](const auto& profile) {
            using Profile = std::decay_t<decltype(profile)>;
            if constexpr (std::is_same_v<Profile,
                              ExtrusionRequest::PolygonProfile>) {
                if (profile.vertices.size() < 3) {
                    throw std::invalid_argument(
                        "Extrusion profile requires at least three vertices");
                }
                for (const auto& point : profile.vertices) {
                    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                        !std::isfinite(point.z)) {
                        throw std::invalid_argument(
                            "Extrusion profile coordinates must be finite");
                    }
                }
            } else if constexpr (std::is_same_v<Profile,
                                     ExtrusionRequest::CircleProfile>) {
                if (!std::isfinite(profile.center.x) ||
                       !std::isfinite(profile.center.y) ||
                       !std::isfinite(profile.center.z) ||
                       !std::isfinite(profile.radius) || profile.radius <= 0.0) {
                    throw std::invalid_argument(
                        "Extrusion Circle must have a finite center and positive radius");
                }
            } else if constexpr (std::is_same_v<Profile,
                                     ExtrusionRequest::EllipseProfile>) {
                const double axis_length = std::sqrt(
                    profile.major_axis_direction.x * profile.major_axis_direction.x +
                    profile.major_axis_direction.y * profile.major_axis_direction.y +
                    profile.major_axis_direction.z * profile.major_axis_direction.z);
                const double normal_length = std::sqrt(
                    request.direction.x * request.direction.x +
                    request.direction.y * request.direction.y +
                    request.direction.z * request.direction.z);
                const double orthogonality = std::abs(
                    profile.major_axis_direction.x * request.direction.x +
                    profile.major_axis_direction.y * request.direction.y +
                    profile.major_axis_direction.z * request.direction.z);
                if (!std::isfinite(profile.center.x) ||
                    !std::isfinite(profile.center.y) ||
                    !std::isfinite(profile.center.z) ||
                    !std::isfinite(axis_length) || axis_length <= 1.0e-12 ||
                    !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
                    orthogonality > 1.0e-9 * axis_length * normal_length ||
                    !std::isfinite(profile.major_radius) ||
                    !std::isfinite(profile.minor_radius) ||
                    profile.major_radius <= 0.0 || profile.minor_radius <= 0.0 ||
                    profile.major_radius < profile.minor_radius) {
                    throw std::invalid_argument(
                        "Extrusion Ellipse must have finite axes and positive ordered radii");
                }
            } else {
                const bool single_periodic_spline = profile.curves.size() == 1 &&
                    std::holds_alternative<ExtrusionRequest::BSplineCurve>(
                        profile.curves.front()) &&
                    std::get<ExtrusionRequest::BSplineCurve>(
                        profile.curves.front()).periodic;
                if (profile.curves.size() < 2 && !single_periodic_spline) {
                    throw std::invalid_argument(
                        "Curved Extrusion profile requires at least two curves");
                }
                const auto finite = [](const Vec3& point) {
                    return std::isfinite(point.x) && std::isfinite(point.y) &&
                        std::isfinite(point.z);
                };
                const auto distance = [](const Vec3& first, const Vec3& second) {
                    return std::sqrt(
                        (first.x - second.x) * (first.x - second.x) +
                        (first.y - second.y) * (first.y - second.y) +
                        (first.z - second.z) * (first.z - second.z));
                };
                std::optional<Vec3> first_start;
                std::optional<Vec3> previous_end;
                for (const auto& curve : profile.curves) {
                    std::visit([&](const auto& exact_curve) {
                        if (!finite(exact_curve.start) || !finite(exact_curve.end)) {
                            throw std::invalid_argument(
                                "Extrusion curve coordinates must be finite");
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::ArcCurve>) {
                            if (!finite(exact_curve.middle)) {
                                throw std::invalid_argument(
                                    "Extrusion Arc coordinates must be finite");
                            }
                            const Vec3 first_vector{
                                exact_curve.middle.x - exact_curve.start.x,
                                exact_curve.middle.y - exact_curve.start.y,
                                exact_curve.middle.z - exact_curve.start.z};
                            const Vec3 second_vector{
                                exact_curve.end.x - exact_curve.start.x,
                                exact_curve.end.y - exact_curve.start.y,
                                exact_curve.end.z - exact_curve.start.z};
                            const Vec3 cross{
                                first_vector.y * second_vector.z -
                                    first_vector.z * second_vector.y,
                                first_vector.z * second_vector.x -
                                    first_vector.x * second_vector.z,
                                first_vector.x * second_vector.y -
                                    first_vector.y * second_vector.x};
                            if (std::sqrt(cross.x * cross.x + cross.y * cross.y +
                                          cross.z * cross.z) <= 1.0e-12) {
                                throw std::invalid_argument(
                                    "Extrusion Arc points must not be collinear");
                            }
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::EllipticalArcCurve>) {
                            const double axis_length = std::sqrt(
                                exact_curve.major_axis_direction.x *
                                    exact_curve.major_axis_direction.x +
                                exact_curve.major_axis_direction.y *
                                    exact_curve.major_axis_direction.y +
                                exact_curve.major_axis_direction.z *
                                    exact_curve.major_axis_direction.z);
                            const double normal_length = std::sqrt(
                                request.direction.x * request.direction.x +
                                request.direction.y * request.direction.y +
                                request.direction.z * request.direction.z);
                            const double orthogonality = std::abs(
                                exact_curve.major_axis_direction.x * request.direction.x +
                                exact_curve.major_axis_direction.y * request.direction.y +
                                exact_curve.major_axis_direction.z * request.direction.z);
                            if (!finite(exact_curve.center) ||
                                !finite(exact_curve.major_axis_direction) ||
                                !std::isfinite(axis_length) || axis_length <= 1.0e-12 ||
                                !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
                                orthogonality > 1.0e-9 * axis_length * normal_length ||
                                !std::isfinite(exact_curve.major_radius) ||
                                !std::isfinite(exact_curve.minor_radius) ||
                                !std::isfinite(exact_curve.start_parameter) ||
                                !std::isfinite(exact_curve.end_parameter) ||
                                exact_curve.major_radius <= 0.0 ||
                                exact_curve.minor_radius <= 0.0 ||
                                exact_curve.major_radius < exact_curve.minor_radius ||
                                exact_curve.end_parameter <= exact_curve.start_parameter ||
                                exact_curve.end_parameter - exact_curve.start_parameter >=
                                    2.0 * std::numbers::pi) {
                                throw std::invalid_argument(
                                    "Extrusion elliptical Arc definition is invalid");
                            }
                            const double orientation = exact_curve.reversed ? -1.0 : 1.0;
                            const Vec3 axis{
                                exact_curve.major_axis_direction.x / axis_length,
                                exact_curve.major_axis_direction.y / axis_length,
                                exact_curve.major_axis_direction.z / axis_length};
                            const Vec3 normal{
                                orientation * request.direction.x / normal_length,
                                orientation * request.direction.y / normal_length,
                                orientation * request.direction.z / normal_length};
                            const Vec3 side{
                                normal.y * axis.z - normal.z * axis.y,
                                normal.z * axis.x - normal.x * axis.z,
                                normal.x * axis.y - normal.y * axis.x};
                            const auto point_at = [&](double parameter) {
                                return Vec3{
                                    exact_curve.center.x + exact_curve.major_radius *
                                        std::cos(parameter) * axis.x +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.x,
                                    exact_curve.center.y + exact_curve.major_radius *
                                        std::cos(parameter) * axis.y +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.y,
                                    exact_curve.center.z + exact_curve.major_radius *
                                        std::cos(parameter) * axis.z +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.z};
                            };
                            const auto expected_start = point_at(
                                exact_curve.start_parameter);
                            const auto expected_end = point_at(
                                exact_curve.end_parameter);
                            const double coordinate_tolerance = 1.0e-7 * std::max(
                                {1.0, exact_curve.major_radius,
                                 exact_curve.minor_radius});
                            if (distance(expected_start, exact_curve.start) >
                                    coordinate_tolerance ||
                                distance(expected_end, exact_curve.end) >
                                    coordinate_tolerance) {
                                throw std::invalid_argument(
                                    "Extrusion elliptical Arc endpoints are inconsistent");
                            }
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::BSplineCurve>) {
                            if (exact_curve.degree < 1 ||
                                exact_curve.control_points.size() <
                                    static_cast<std::size_t>(exact_curve.degree) + 1 ||
                                std::any_of(exact_curve.control_points.begin(),
                                    exact_curve.control_points.end(),
                                    [&](const auto& point) { return !finite(point); })) {
                                throw std::invalid_argument(
                                    "Extrusion B-spline definition is invalid");
                            }
                        }
                        if (distance(exact_curve.start, exact_curve.end) <= 1.0e-12 &&
                            !std::is_same_v<std::decay_t<decltype(exact_curve)>,
                                ExtrusionRequest::BSplineCurve>) {
                            throw std::invalid_argument(
                                "Extrusion curve must have distinct endpoints");
                        }
                        if (!first_start) first_start = exact_curve.start;
                        if (previous_end &&
                            distance(*previous_end, exact_curve.start) > 1.0e-7) {
                            throw std::invalid_argument(
                                "Extrusion Curved profile is not connected");
                        }
                        previous_end = exact_curve.end;
                    }, curve);
                }
                if (!first_start || !previous_end ||
                    distance(*previous_end, *first_start) > 1.0e-7) {
                    throw std::invalid_argument(
                        "Extrusion Curved profile is not closed");
                }
            }
        }, profile_variant);
    };
    validate_profile(request.outer_profile);
    for (const auto& profile : request.inner_profiles) validate_profile(profile);
    for (const auto& region : request.additional_profile_regions) {
        validate_profile(region.outer_profile);
        for (const auto& profile : region.inner_profiles) validate_profile(profile);
    }
    const double length = std::sqrt(
        request.direction.x * request.direction.x +
        request.direction.y * request.direction.y +
        request.direction.z * request.direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("Extrusion direction must be finite and non-zero");
    }
    if (request.extent == ExtrusionRequest::Extent::UpToPlane) {
        const double normal_length = std::sqrt(
            request.target_plane_normal.x * request.target_plane_normal.x +
            request.target_plane_normal.y * request.target_plane_normal.y +
            request.target_plane_normal.z * request.target_plane_normal.z);
        const double dot = request.direction.x * request.target_plane_normal.x +
            request.direction.y * request.target_plane_normal.y +
            request.direction.z * request.target_plane_normal.z;
        if (!request.target_face.valid() || !request.target_face.instance_path.empty() ||
            !std::isfinite(request.target_plane_origin.x) ||
            !std::isfinite(request.target_plane_origin.y) ||
            !std::isfinite(request.target_plane_origin.z) ||
            !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
            std::abs(dot) <= 1.0e-12) {
            throw std::invalid_argument("Extrusion target plane is invalid or parallel");
        }
    } else if (request.extent == ExtrusionRequest::Extent::UpToSurface) {
        if (!request.target_face.valid() || request.target_is_datum ||
            request.target_surface_triangles.empty() ||
            request.target_surface_triangles.size() % 3 != 0 ||
            std::any_of(request.target_surface_triangles.begin(),
                request.target_surface_triangles.end(), [](const auto& point) {
                    return !std::isfinite(point.x) || !std::isfinite(point.y) ||
                           !std::isfinite(point.z);
                })) {
            throw std::invalid_argument("Extrusion target surface is invalid");
        }
    }
}

TopoDS_Wire make_profile_wire(
    const ExtrusionRequest::ProfileLoop& profile_variant,
    const Vec3& profile_normal) {
    return std::visit([&](const auto& profile) {
        using Profile = std::decay_t<decltype(profile)>;
        if constexpr (std::is_same_v<Profile,
                          ExtrusionRequest::PolygonProfile>) {
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& point : profile.vertices) {
                polygon.Add(gp_Pnt(point.x, point.y, point.z));
            }
            polygon.Close();
            if (!polygon.IsDone()) {
                throw std::runtime_error("OCCT polygon profile wire failed");
            }
            return polygon.Wire();
        } else if constexpr (std::is_same_v<Profile,
                                 ExtrusionRequest::CircleProfile>) {
            const gp_Dir normal(
                profile_normal.x, profile_normal.y, profile_normal.z);
            BRepBuilderAPI_MakeEdge edge(gp_Circ(
                gp_Ax2(gp_Pnt(profile.center.x, profile.center.y, profile.center.z),
                       normal),
                profile.radius));
            if (!edge.IsDone()) {
                throw std::runtime_error("OCCT circular profile edge failed");
            }
            BRepBuilderAPI_MakeWire circle_wire(edge.Edge());
            if (!circle_wire.IsDone()) {
                throw std::runtime_error("OCCT circular profile wire failed");
            }
            return circle_wire.Wire();
        } else if constexpr (std::is_same_v<Profile,
                                 ExtrusionRequest::EllipseProfile>) {
            const gp_Dir normal(
                profile_normal.x, profile_normal.y, profile_normal.z);
            const gp_Dir major_direction(
                profile.major_axis_direction.x,
                profile.major_axis_direction.y,
                profile.major_axis_direction.z);
            BRepBuilderAPI_MakeEdge edge(gp_Elips(
                gp_Ax2(gp_Pnt(profile.center.x, profile.center.y, profile.center.z),
                       normal, major_direction),
                profile.major_radius, profile.minor_radius));
            if (!edge.IsDone()) {
                throw std::runtime_error("OCCT elliptical profile edge failed");
            }
            BRepBuilderAPI_MakeWire ellipse_wire(edge.Edge());
            if (!ellipse_wire.IsDone()) {
                throw std::runtime_error("OCCT elliptical profile wire failed");
            }
            return ellipse_wire.Wire();
        } else {
            BRepBuilderAPI_MakeWire curved_wire;
            for (const auto& curve : profile.curves) {
                const TopoDS_Edge edge = std::visit([&](const auto& exact_curve) {
                    using Curve = std::decay_t<decltype(exact_curve)>;
                    if constexpr (std::is_same_v<Curve,
                                      ExtrusionRequest::LineCurve>) {
                        return BRepBuilderAPI_MakeEdge(
                            gp_Pnt(exact_curve.start.x, exact_curve.start.y,
                                   exact_curve.start.z),
                            gp_Pnt(exact_curve.end.x, exact_curve.end.y,
                                   exact_curve.end.z)).Edge();
                    } else if constexpr (std::is_same_v<Curve,
                                             ExtrusionRequest::ArcCurve>) {
                        GC_MakeArcOfCircle arc(
                            gp_Pnt(exact_curve.start.x, exact_curve.start.y,
                                   exact_curve.start.z),
                            gp_Pnt(exact_curve.middle.x, exact_curve.middle.y,
                                   exact_curve.middle.z),
                            gp_Pnt(exact_curve.end.x, exact_curve.end.y,
                                   exact_curve.end.z));
                        if (!arc.IsDone()) {
                            throw std::runtime_error("OCCT profile Arc failed");
                        }
                        return BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
                    } else if constexpr (std::is_same_v<Curve,
                                             ExtrusionRequest::EllipticalArcCurve>) {
                        gp_Dir normal(
                            profile_normal.x, profile_normal.y, profile_normal.z);
                        if (exact_curve.reversed) normal.Reverse();
                        const gp_Dir major_direction(
                            exact_curve.major_axis_direction.x,
                            exact_curve.major_axis_direction.y,
                            exact_curve.major_axis_direction.z);
                        const gp_Elips ellipse(
                            gp_Ax2(gp_Pnt(
                                exact_curve.center.x, exact_curve.center.y,
                                exact_curve.center.z), normal, major_direction),
                            exact_curve.major_radius, exact_curve.minor_radius);
                        BRepBuilderAPI_MakeEdge edge(
                            ellipse, exact_curve.start_parameter,
                            exact_curve.end_parameter);
                        if (!edge.IsDone()) {
                            throw std::runtime_error(
                                "OCCT profile elliptical Arc failed");
                        }
                        return edge.Edge();
                    } else {
                        const Standard_Integer pole_count =
                            static_cast<Standard_Integer>(exact_curve.control_points.size());
                        TColgp_Array1OfPnt poles(1, pole_count);
                        for (Standard_Integer index = 1; index <= pole_count; ++index) {
                            const auto& point = exact_curve.control_points[
                                static_cast<std::size_t>(index - 1)];
                            poles.SetValue(index, gp_Pnt(point.x, point.y, point.z));
                        }
                        const Standard_Integer knot_count = exact_curve.periodic
                            ? pole_count + 1 : pole_count -
                                static_cast<Standard_Integer>(exact_curve.degree) + 1;
                        TColStd_Array1OfReal knots(1, knot_count);
                        TColStd_Array1OfInteger multiplicities(1, knot_count);
                        for (Standard_Integer index = 1; index <= knot_count; ++index) {
                            knots.SetValue(index, static_cast<double>(index - 1));
                            multiplicities.SetValue(index, exact_curve.periodic ? 1
                                : (index == 1 || index == knot_count)
                                    ? static_cast<Standard_Integer>(exact_curve.degree) + 1
                                    : 1);
                        }
                        Handle(Geom_BSplineCurve) bspline = new Geom_BSplineCurve(
                            poles, knots, multiplicities,
                            static_cast<Standard_Integer>(exact_curve.degree),
                            exact_curve.periodic);
                        BRepBuilderAPI_MakeEdge edge(bspline);
                        if (!edge.IsDone()) {
                            throw std::runtime_error("OCCT profile B-spline failed");
                        }
                        return edge.Edge();
                    }
                }, curve);
                curved_wire.Add(edge);
            }
            if (!curved_wire.IsDone()) {
                throw std::runtime_error("OCCT curved profile wire failed");
            }
            return curved_wire.Wire();
        }
    }, profile_variant);
}

PrimitiveData make_extrusion_data(
    const ExtrusionRequest& request, const std::string& owner_id,
    const std::optional<TopoDS_Face>& exact_target = std::nullopt) {
    std::vector<TopoDS_Wire> wires{
        make_profile_wire(request.outer_profile, request.direction)};
    BRepBuilderAPI_MakeFace face_builder(wires.front(), true);
    for (const auto& inner_profile : request.inner_profiles) {
        auto inner_wire = make_profile_wire(inner_profile, request.direction);
        inner_wire.Reverse();
        wires.push_back(std::move(inner_wire));
        face_builder.Add(wires.back());
    }
    if (!face_builder.IsDone()) throw std::runtime_error("OCCT profile face failed");
    TopoDS_Face face = face_builder.Face();
    if (!BRepCheck_Analyzer(face).IsValid()) {
        throw std::runtime_error("OCCT profile face is invalid");
    }
    const double direction_length = std::sqrt(
        request.direction.x * request.direction.x +
        request.direction.y * request.direction.y +
        request.direction.z * request.direction.z);
    const Vec3 unit{request.direction.x / direction_length,
                    request.direction.y / direction_length,
                    request.direction.z / direction_length};
    Vec3 prism_direction = request.direction;
    if (request.extent == ExtrusionRequest::Extent::UpToPlane) {
        const auto& normal = request.target_plane_normal;
        const double denominator = unit.x * normal.x + unit.y * normal.y +
                                   unit.z * normal.z;
        double maximum_distance = 0.0;
        for (const auto& wire : wires) {
            for (TopExp_Explorer explorer(wire, TopAbs_VERTEX);
                 explorer.More(); explorer.Next()) {
                const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current()));
                const double distance_to_plane =
                    ((request.target_plane_origin.x - point.X()) * normal.x +
                     (request.target_plane_origin.y - point.Y()) * normal.y +
                     (request.target_plane_origin.z - point.Z()) * normal.z) /
                    denominator;
                if (!std::isfinite(distance_to_plane) || distance_to_plane <= 1.0e-9) {
                    throw std::runtime_error(
                        "Extrusion profile crosses or lies beyond target plane");
                }
                maximum_distance = std::max(maximum_distance, distance_to_plane);
            }
        }
        const double overrun = maximum_distance +
            std::max(1.0, maximum_distance * 0.01);
        prism_direction = {unit.x * overrun, unit.y * overrun, unit.z * overrun};
    } else if (request.extent == ExtrusionRequest::Extent::UpToSurface) {
        const auto ray_distance = [&](const gp_Pnt& point) {
            double nearest = std::numeric_limits<double>::infinity();
            const auto& triangles = request.target_surface_triangles;
            for (std::size_t index = 0; index < triangles.size(); index += 3) {
                const auto& v0 = triangles[index];
                const auto& v1 = triangles[index + 1];
                const auto& v2 = triangles[index + 2];
                const Vec3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                const Vec3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                const Vec3 h{unit.y * edge2.z - unit.z * edge2.y,
                             unit.z * edge2.x - unit.x * edge2.z,
                             unit.x * edge2.y - unit.y * edge2.x};
                const double determinant =
                    edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
                if (std::abs(determinant) <= 1e-12) continue;
                const double inverse = 1.0 / determinant;
                const Vec3 s{point.X() - v0.x, point.Y() - v0.y, point.Z() - v0.z};
                const double u = inverse * (s.x * h.x + s.y * h.y + s.z * h.z);
                if (u < -1e-8 || u > 1.0 + 1e-8) continue;
                const Vec3 q{s.y * edge1.z - s.z * edge1.y,
                             s.z * edge1.x - s.x * edge1.z,
                             s.x * edge1.y - s.y * edge1.x};
                const double v = inverse *
                    (unit.x * q.x + unit.y * q.y + unit.z * q.z);
                if (v < -1e-8 || u + v > 1.0 + 1e-8) continue;
                const double distance = inverse *
                    (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
                if (distance > 1e-8) nearest = std::min(nearest, distance);
            }
            if (!std::isfinite(nearest)) {
                throw std::runtime_error("Extrusion profile misses target surface");
            }
            return nearest;
        };
        double maximum_distance = 0.0;
        for (const auto& wire : wires) {
            for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
                 explorer.More(); explorer.Next()) {
                BRepAdaptor_Curve curve(TopoDS::Edge(explorer.Current()));
                const double first = curve.FirstParameter();
                const double last = curve.LastParameter();
                constexpr int samples = 32;
                for (int sample = 0; sample <= samples; ++sample) {
                    const double parameter = first +
                        (last - first) * static_cast<double>(sample) / samples;
                    maximum_distance = std::max(
                        maximum_distance, ray_distance(curve.Value(parameter)));
                }
            }
        }
        const double overrun = maximum_distance +
            std::max(1.0, maximum_distance * 0.01);
        prism_direction = {unit.x * overrun, unit.y * overrun, unit.z * overrun};
    } else if (request.extent == ExtrusionRequest::Extent::ThroughAll) {
        constexpr double half_span = 2'000'000.0;
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(-unit.x * half_span,
                                    -unit.y * half_span,
                                    -unit.z * half_span));
        for (auto& wire : wires) {
            wire = TopoDS::Wire(BRepBuilderAPI_Transform(wire, shift, true).Shape());
        }
        BRepBuilderAPI_MakeFace shifted_face(wires.front(), true);
        for (std::size_t index = 1; index < wires.size(); ++index) {
            shifted_face.Add(wires[index]);
        }
        if (!shifted_face.IsDone()) {
            throw std::runtime_error("OCCT Through-all profile shift failed");
        }
        face = shifted_face.Face();
        prism_direction = {unit.x * 2.0 * half_span,
                           unit.y * 2.0 * half_span,
                           unit.z * 2.0 * half_span};
    }
    BRepPrimAPI_MakePrism prism(face, gp_Vec(
        prism_direction.x, prism_direction.y, prism_direction.z), true, true);
    prism.Build();
    if (!prism.IsDone() || !BRepCheck_Analyzer(prism.Shape()).IsValid()) {
        throw std::runtime_error("OCCT extrusion failed or produced an invalid solid");
    }
    PrimitiveData result{prism.Shape(), {}, {}, {}};
    result.faces.push_back({prism.FirstShape(), {owner_id, "profile_start"}});
    result.faces.push_back({prism.LastShape(), {owner_id, "profile_end"}});
    std::vector<std::string> boundary_ids{request.outer_boundary_id};
    boundary_ids.insert(boundary_ids.end(), request.inner_boundary_ids.begin(),
                        request.inner_boundary_ids.end());
    std::size_t edge_index{};
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        const auto& wire = wires[wire_index];
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next(), ++boundary_edge) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto index = std::to_string(edge_index++);
            const auto stable_index = wire_index < boundary_ids.size() &&
                    !boundary_ids[wire_index].empty()
                ? "profile-boundary:" + boundary_ids[wire_index] +
                    ":edge:" + std::to_string(boundary_edge)
                : "side:" + index;
            const auto& generated = prism.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, stable_index}});
                }
            }
            const auto first = prism.FirstShape(edge);
            const auto last = prism.LastShape(edge);
            if (!first.IsNull()) {
                result.edges.push_back({first, {owner_id, "edge:start:" + index}});
            }
            if (!last.IsNull()) {
                result.edges.push_back({last, {owner_id, "edge:end:" + index}});
            }
        }
    }
    std::size_t vertex_index{};
    for (const auto& wire : wires) {
        for (TopExp_Explorer explorer(wire, TopAbs_VERTEX);
             explorer.More(); explorer.Next()) {
            const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
            const auto index = std::to_string(vertex_index++);
            const auto first = prism.FirstShape(vertex);
            const auto last = prism.LastShape(vertex);
            if (!first.IsNull()) {
                result.vertices.push_back({first, {owner_id, "vertex:start:" + index}});
            }
            if (!last.IsNull()) {
                result.vertices.push_back({last, {owner_id, "vertex:end:" + index}});
            }
            const auto& generated = prism.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id, "edge:vertical:" + index}});
                }
            }
        }
    }
    if (request.extent == ExtrusionRequest::Extent::UpToPlane ||
        request.extent == ExtrusionRequest::Extent::UpToSurface) {
        TopoDS_Face limiting_face;
        gp_Pnt keep_point;
        if (request.extent == ExtrusionRequest::Extent::UpToPlane) {
            const gp_Dir plane_normal(request.target_plane_normal.x,
                                  request.target_plane_normal.y,
                                  request.target_plane_normal.z);
            const gp_Pln plane(gp_Pnt(request.target_plane_origin.x,
                                  request.target_plane_origin.y,
                                  request.target_plane_origin.z), plane_normal);
            BRepBuilderAPI_MakeFace plane_face(plane, -5'000'000.0, 5'000'000.0,
                                           -5'000'000.0, 5'000'000.0);
            limiting_face = plane_face.Face();
            const double side = unit.x * request.target_plane_normal.x +
                            unit.y * request.target_plane_normal.y +
                            unit.z * request.target_plane_normal.z;
            keep_point = gp_Pnt(
                request.target_plane_origin.x - std::copysign(1.0, side) * plane_normal.X(),
                request.target_plane_origin.y - std::copysign(1.0, side) * plane_normal.Y(),
                request.target_plane_origin.z - std::copysign(1.0, side) * plane_normal.Z());
        } else {
            if (!exact_target) {
                throw std::runtime_error("Exact Extrusion target surface is missing");
            }
            limiting_face = *exact_target;
            const auto first_vertex = TopExp_Explorer(face, TopAbs_VERTEX);
            if (!first_vertex.More()) {
                throw std::runtime_error("Extrusion profile has no reference point");
            }
            keep_point = BRep_Tool::Pnt(TopoDS::Vertex(first_vertex.Current()));
        }
        BRepPrimAPI_MakeHalfSpace half_space(limiting_face, keep_point);
        BRepAlgoAPI_Common clip(result.shape, half_space.Solid());
        clip.SetToFillHistory(true);
        clip.Build();
        if (!clip.IsDone() || clip.Shape().IsNull() ||
            !BRepCheck_Analyzer(clip.Shape()).IsValid()) {
            throw std::runtime_error("OCCT Up-to-plane clipping failed");
        }
        result.faces = propagate_topology(clip, result.faces, {});
        result.edges = propagate_topology(clip, result.edges, {});
        result.vertices = propagate_topology(clip, result.vertices, {});
        result.shape = clip.Shape();
        const auto contains_shape = [](const auto& owned, const auto& shape) {
            return std::any_of(owned.begin(), owned.end(),
                [&](const auto& item) { return item.shape.IsSame(shape); });
        };
        std::size_t generated_face{};
        for (TopExp_Explorer explorer(result.shape, TopAbs_FACE);
             explorer.More(); explorer.Next()) {
            if (!contains_shape(result.faces, explorer.Current())) {
                result.faces.push_back({explorer.Current(),
                    {owner_id, "up_to_face:" + std::to_string(generated_face++)}});
            }
        }
        std::size_t generated_edge{};
        for (TopExp_Explorer explorer(result.shape, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            if (!contains_shape(result.edges, explorer.Current())) {
                result.edges.push_back({explorer.Current(),
                    {owner_id, "up_to_edge:" + std::to_string(generated_edge++)}});
            }
        }
        std::size_t generated_vertex{};
        for (TopExp_Explorer explorer(result.shape, TopAbs_VERTEX);
             explorer.More(); explorer.Next()) {
            if (!contains_shape(result.vertices, explorer.Current())) {
                result.vertices.push_back({explorer.Current(),
                    {owner_id, "up_to_vertex:" + std::to_string(generated_vertex++)}});
            }
        }
    }
    if (!request.additional_profile_regions.empty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        builder.Add(compound, result.shape);
        for (std::size_t index = 0;
             index < request.additional_profile_regions.size(); ++index) {
            auto additional_request = request;
            additional_request.outer_profile =
                request.additional_profile_regions[index].outer_profile;
            additional_request.inner_profiles =
                request.additional_profile_regions[index].inner_profiles;
            additional_request.profile_region_id =
                request.additional_profile_regions[index].region_id;
            additional_request.outer_boundary_id =
                request.additional_profile_regions[index].outer_boundary_id;
            additional_request.inner_boundary_ids =
                request.additional_profile_regions[index].inner_boundary_ids;
            additional_request.additional_profile_regions.clear();
            auto additional = make_extrusion_data(
                additional_request, owner_id, exact_target);
            const auto prefix = "region:" + std::to_string(index + 1) + ":";
            for (auto& face : additional.faces) face.reference.semantic_key =
                prefix + face.reference.semantic_key;
            for (auto& edge : additional.edges) edge.reference.semantic_key =
                prefix + edge.reference.semantic_key;
            for (auto& vertex : additional.vertices) vertex.reference.semantic_key =
                prefix + vertex.reference.semantic_key;
            builder.Add(compound, additional.shape);
            result.faces.insert(result.faces.end(),
                std::make_move_iterator(additional.faces.begin()),
                std::make_move_iterator(additional.faces.end()));
            result.edges.insert(result.edges.end(),
                std::make_move_iterator(additional.edges.begin()),
                std::make_move_iterator(additional.edges.end()));
            result.vertices.insert(result.vertices.end(),
                std::make_move_iterator(additional.vertices.begin()),
                std::make_move_iterator(additional.vertices.end()));
        }
        result.shape = compound;
    }
    return result;
}

PrimitiveData make_step_data(
    const StepRequest& request, const std::string& owner_id,
    std::unordered_map<std::string, Handle(TDocStd_Document)>& documents) {
    if (request.source_path.empty()) throw std::invalid_argument("STEP source path is empty");
    TopoDS_Shape imported;
    if (request.component_path.empty()) {
        STEPControl_Reader reader;
        if (reader.ReadFile(request.source_path.c_str()) != IFSelect_RetDone ||
            reader.TransferRoots() == 0) {
            throw std::runtime_error("OCCT STEP import failed");
        }
        imported = reader.OneShape();
    } else {
        auto& document = documents[request.source_path];
        if (document.IsNull()) {
            XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", document);
            STEPCAFControl_Reader reader;
            if (reader.ReadFile(request.source_path.c_str()) != IFSelect_RetDone ||
                !reader.Transfer(document)) {
                throw std::runtime_error("OCCT STEP product structure import failed");
            }
        }
        TDF_Label definition;
        TDF_Tool::Label(
            document->GetData(), request.component_path.c_str(), definition, false);
        imported = definition.IsNull() ? TopoDS_Shape{}
            : XCAFDoc_DocumentTool::ShapeTool(document->Main())->GetShape(definition);
        if (imported.IsNull()) throw std::runtime_error("STEP component is missing");
    }
    PrimitiveData result{std::move(imported), {}, {}, {}};
    if (result.shape.IsNull() || !BRepCheck_Analyzer(result.shape).IsValid()) {
        throw std::runtime_error("STEP did not produce a valid shape");
    }
    std::size_t index{};
    for (TopExp_Explorer explorer(result.shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        result.faces.push_back({explorer.Current(), {owner_id, "step-face:" + std::to_string(index++)}});
    }
    index = 0;
    for (TopExp_Explorer explorer(result.shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        result.edges.push_back({explorer.Current(), {owner_id, "step-edge:" + std::to_string(index++)}});
    }
    index = 0;
    for (TopExp_Explorer explorer(result.shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        result.vertices.push_back({explorer.Current(), {owner_id, "step-vertex:" + std::to_string(index++)}});
    }
    return result;
}

void validate_revolution(const RevolutionRequest& request) {
    ExtrusionRequest profile_request;
    profile_request.outer_profile = request.outer_profile;
    profile_request.inner_profiles = request.inner_profiles;
    profile_request.additional_profile_regions = request.additional_profile_regions;
    profile_request.direction = request.profile_normal;
    validate_extrusion(profile_request);
    const double axis_length = std::sqrt(
        request.axis_direction.x * request.axis_direction.x +
        request.axis_direction.y * request.axis_direction.y +
        request.axis_direction.z * request.axis_direction.z);
    if (!std::isfinite(request.axis_point.x) ||
        !std::isfinite(request.axis_point.y) ||
        !std::isfinite(request.axis_point.z) || !std::isfinite(axis_length) ||
        axis_length <= 1.0e-12 || !std::isfinite(request.angle_degrees) ||
        request.angle_degrees <= 0.0 || request.angle_degrees > 360.0) {
        throw std::invalid_argument("Revolution axis or angle is invalid");
    }
}

PrimitiveData make_revolution_data(
    const RevolutionRequest& request, const std::string& owner_id) {
    std::vector<TopoDS_Wire> wires{
        make_profile_wire(request.outer_profile, request.profile_normal)};
    BRepBuilderAPI_MakeFace face_builder(wires.front(), true);
    for (const auto& inner_profile : request.inner_profiles) {
        auto inner_wire = make_profile_wire(inner_profile, request.profile_normal);
        inner_wire.Reverse();
        wires.push_back(std::move(inner_wire));
        face_builder.Add(wires.back());
    }
    if (!face_builder.IsDone() || !BRepCheck_Analyzer(face_builder.Face()).IsValid()) {
        throw std::runtime_error("OCCT Revolution profile face is invalid");
    }
    BRepPrimAPI_MakeRevol revolution(
        face_builder.Face(),
        gp_Ax1(gp_Pnt(request.axis_point.x, request.axis_point.y,
                      request.axis_point.z),
               gp_Dir(request.axis_direction.x, request.axis_direction.y,
                      request.axis_direction.z)),
        request.angle_degrees * std::numbers::pi / 180.0, true);
    revolution.Build();
    if (!revolution.IsDone() ||
        !BRepCheck_Analyzer(revolution.Shape()).IsValid()) {
        throw std::runtime_error("OCCT Revolution failed or produced an invalid solid");
    }
    PrimitiveData result{revolution.Shape(), {}, {}, {}};
    if (request.angle_degrees < 360.0 - 1.0e-9) {
        result.faces.push_back(
            {revolution.FirstShape(), {owner_id, "profile_start"}});
        result.faces.push_back(
            {revolution.LastShape(), {owner_id, "profile_end"}});
    }
    std::vector<std::string> boundary_ids{request.outer_boundary_id};
    boundary_ids.insert(boundary_ids.end(), request.inner_boundary_ids.begin(),
                        request.inner_boundary_ids.end());
    std::size_t edge_index{};
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        const auto& wire = wires[wire_index];
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next(), ++boundary_edge) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto index = std::to_string(edge_index++);
            const auto stable_index = wire_index < boundary_ids.size() &&
                    !boundary_ids[wire_index].empty()
                ? "profile-boundary:" + boundary_ids[wire_index] +
                    ":edge:" + std::to_string(boundary_edge)
                : "side:" + index;
            const auto& generated = revolution.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, stable_index}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(edge);
                const auto last = revolution.LastShape(edge);
                if (!first.IsNull()) {
                    result.edges.push_back(
                        {first, {owner_id, "edge:start:" + index}});
                }
                if (!last.IsNull()) {
                    result.edges.push_back(
                        {last, {owner_id, "edge:end:" + index}});
                }
            }
        }
    }
    std::size_t vertex_index{};
    for (const auto& wire : wires) {
        for (TopExp_Explorer explorer(wire, TopAbs_VERTEX);
             explorer.More(); explorer.Next()) {
            const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
            const auto index = std::to_string(vertex_index++);
            const auto& generated = revolution.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id, "edge:revolved:" + index}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(vertex);
                const auto last = revolution.LastShape(vertex);
                if (!first.IsNull()) {
                    result.vertices.push_back(
                        {first, {owner_id, "vertex:start:" + index}});
                }
                if (!last.IsNull()) {
                    result.vertices.push_back(
                        {last, {owner_id, "vertex:end:" + index}});
                }
            }
        }
    }
    if (!request.additional_profile_regions.empty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        builder.Add(compound, result.shape);
        for (std::size_t index = 0;
             index < request.additional_profile_regions.size(); ++index) {
            auto additional_request = request;
            additional_request.outer_profile =
                request.additional_profile_regions[index].outer_profile;
            additional_request.inner_profiles =
                request.additional_profile_regions[index].inner_profiles;
            additional_request.profile_region_id =
                request.additional_profile_regions[index].region_id;
            additional_request.outer_boundary_id =
                request.additional_profile_regions[index].outer_boundary_id;
            additional_request.inner_boundary_ids =
                request.additional_profile_regions[index].inner_boundary_ids;
            additional_request.additional_profile_regions.clear();
            auto additional = make_revolution_data(additional_request, owner_id);
            const auto prefix = "region:" + std::to_string(index + 1) + ":";
            for (auto& face : additional.faces) face.reference.semantic_key =
                prefix + face.reference.semantic_key;
            for (auto& edge : additional.edges) edge.reference.semantic_key =
                prefix + edge.reference.semantic_key;
            for (auto& vertex : additional.vertices) vertex.reference.semantic_key =
                prefix + vertex.reference.semantic_key;
            builder.Add(compound, additional.shape);
            result.faces.insert(result.faces.end(),
                std::make_move_iterator(additional.faces.begin()),
                std::make_move_iterator(additional.faces.end()));
            result.edges.insert(result.edges.end(),
                std::make_move_iterator(additional.edges.begin()),
                std::make_move_iterator(additional.edges.end()));
            result.vertices.insert(result.vertices.end(),
                std::make_move_iterator(additional.vertices.begin()),
                std::make_move_iterator(additional.vertices.end()));
        }
        result.shape = compound;
    }
    return result;
}

template <typename Algorithm, typename Owned>
std::vector<Owned> propagate_topology(
    Algorithm& algorithm,
    const std::vector<Owned>& existing,
    const std::vector<Owned>& operand) {
    std::vector<Owned> propagated;
    auto propagate_one = [&](const Owned& source) {
        bool has_descendant = false;
        for (const auto* list : {
                &algorithm.Modified(source.shape), &algorithm.Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) continue;
                propagated.push_back({iterator.Value(), source.reference});
                has_descendant = true;
            }
        }
        if (!has_descendant && !algorithm.IsDeleted(source.shape)) {
            propagated.push_back(source);
        }
    };
    for (const auto& source : existing) propagate_one(source);
    for (const auto& source : operand) propagate_one(source);
    return propagated;
}

template <typename Reference, typename Owned>
Reference reference_for_shape(
    const TopoDS_Shape& shape, const std::vector<Owned>& owned_shapes) {
    Reference result;
    for (const auto& owned : owned_shapes) {
        if (!shape.IsSame(owned.shape)) continue;
        if (!result.valid()) {
            result = owned.reference;
        } else if (result != owned.reference) {
            // Ambiguous ancestry must remain unselectable until repaired.
            return {};
        }
    }
    return result;
}

BodyResult make_result(
    const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& owned_faces,
    const std::vector<OwnedEdge>& owned_edges,
    const std::vector<OwnedVertex>& owned_vertices,
    bool original_reference_geometry = false) {
    BRepMesh_IncrementalMesh(shape, 0.1, false, 0.5, true).Perform();
    BodyResult result;
    GProp_GProps volume_properties;
    GProp_GProps surface_properties;
    BRepGProp::VolumeProperties(shape, volume_properties);
    BRepGProp::SurfaceProperties(shape, surface_properties);
    result.volume = volume_properties.Mass();
    result.surface_area = surface_properties.Mass();

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation =
            BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        const FaceReference reference = original_reference_geometry
            ? reference_for_shape<FaceReference>(face, owned_faces)
            : FaceReference{};
        const std::uint32_t base =
            static_cast<std::uint32_t>(result.mesh.vertices.size());
        const gp_Trsf transform = location.Transformation();
        for (int node = 1; node <= triangulation->NbNodes(); ++node) {
            const gp_Pnt point = triangulation->Node(node).Transformed(transform);
            result.mesh.vertices.push_back({point.X(), point.Y(), point.Z()});
        }
        for (int triangle = 1; triangle <= triangulation->NbTriangles(); ++triangle) {
            int first{};
            int second{};
            int third{};
            triangulation->Triangle(triangle).Get(first, second, third);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
            result.mesh.triangles.insert(result.mesh.triangles.end(), {
                base + static_cast<std::uint32_t>(first - 1),
                base + static_cast<std::uint32_t>(second - 1),
                base + static_cast<std::uint32_t>(third - 1),
            });
            result.mesh.triangle_references.push_back(reference);
        }
    }
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        const EdgeReference reference = original_reference_geometry
            ? reference_for_shape<EdgeReference>(edge, owned_edges)
            : EdgeReference{};
        BRepAdaptor_Curve curve(edge);
        const int sample_count = curve.GetType() == GeomAbs_Line ? 2 : 33;
        GCPnts_UniformAbscissa samples(curve, sample_count);
        if (!samples.IsDone() || samples.NbPoints() < 2) continue;
        ViewerEdge viewer_edge;
        viewer_edge.reference = reference;
        viewer_edge.points.reserve(static_cast<std::size_t>(samples.NbPoints()));
        for (int index = 1; index <= samples.NbPoints(); ++index) {
            const gp_Pnt point = curve.Value(samples.Parameter(index));
            viewer_edge.points.push_back({point.X(), point.Y(), point.Z()});
        }
        result.mesh.edges.push_back(std::move(viewer_edge));
    }
    if (original_reference_geometry) for (
        TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
        const VertexReference reference =
            reference_for_shape<VertexReference>(vertex, owned_vertices);
        if (!reference.valid()) continue;
        const gp_Pnt point = BRep_Tool::Pnt(vertex);
        result.mesh.points.push_back({
            {point.X(), point.Y(), point.Z()}, reference});
    }
    return result;
}

void append_original_reference_geometry(
    ViewerReferenceGeometry& target, ViewerMesh source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) target.triangles.push_back(offset + index);
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    for (auto& edge : source.edges) {
        if (edge.reference.valid()) target.edges.push_back(std::move(edge));
    }
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

}  // namespace

std::string OcctKernel::name() const {
    return "OCCT";
}

BodyResult OcctKernel::make_box(const BoxRequest& request) const {
    return evaluate_boxes({BoxOperation{"box", request, BooleanOperation::Add}});
}

BodyResult OcctKernel::evaluate_boxes(
    const std::vector<BoxOperation>& operations) const {
    auto boundaries = evaluate_box_boundaries(operations);
    return boundaries.empty() ? BodyResult{} : std::move(boundaries.back());
}

std::vector<BodyResult> OcctKernel::evaluate_box_boundaries(
    const std::vector<BoxOperation>& operations) const {
    std::vector<HistoryOperation> history;
    history.reserve(operations.size());
    for (const auto& operation : operations) {
        history.push_back({operation.owner_id, operation.box, operation.operation});
    }
    return evaluate_history(history);
}

std::vector<BodyResult> OcctKernel::import_step_components(
    const std::vector<StepRequest>& requests) const {
    std::unordered_map<std::string, Handle(TDocStd_Document)> documents;
    std::vector<BodyResult> results;
    results.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const std::string owner = "step-import:" + std::to_string(index);
        const auto data = make_step_data(requests[index], owner, documents);
        auto result = make_result(data.shape, data.faces, data.edges, data.vertices);
        auto references = make_result(
            data.shape, data.faces, data.edges, data.vertices, true);
        append_original_reference_geometry(
            result.mesh.original_references, std::move(references.mesh));
        results.push_back(std::move(result));
    }
    return results;
}

std::vector<BodyResult> OcctKernel::evaluate_history(
    const std::vector<HistoryOperation>& operations) const {
    if (operations.empty()) return {};
    if (operations.front().operation == BooleanOperation::Subtract) {
        throw std::invalid_argument("The first history operation cannot subtract");
    }
    try {
        TopoDS_Shape result_shape;
        std::vector<OwnedFace> owned_faces;
        std::vector<OwnedEdge> owned_edges;
        std::vector<OwnedVertex> owned_vertices;
        ViewerReferenceGeometry original_references;
        std::unordered_map<std::string, Handle(TDocStd_Document)> step_documents;
        std::vector<BodyResult> boundaries;
        boundaries.reserve(operations.size());
        for (const auto& operation : operations) {
            if (operation.owner_id.empty()) {
                throw std::invalid_argument("History operation owner ID is required");
            }
            const auto apply_edge_treatment = [&](const auto& treatment) {
                if (result_shape.IsNull()) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer requires an input body");
                }
                if (treatment.edges.empty() ||
                    std::any_of(treatment.edges.begin(), treatment.edges.end(),
                        [](const auto& edge) {
                            return !edge.valid() || !edge.instance_path.empty();
                        }) ||
                    treatment.origin != EdgeSelectionOrigin::OriginalEntity) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer original edge reference is invalid");
                }
                const double size = [&] {
                    using Treatment = std::decay_t<decltype(treatment)>;
                    if constexpr (std::is_same_v<Treatment, FilletRequest>) {
                        return treatment.radius;
                    } else {
                        return treatment.distance;
                    }
                }();
                if (!std::isfinite(size) || size <= 0.0) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer size must be finite and positive");
                }
                std::vector<TopoDS_Edge> selected;
                for (const auto& requested : treatment.edges) {
                    bool found = false;
                    for (const auto& owned : owned_edges) {
                        if (owned.reference.owner_id == requested.owner_id &&
                            owned.reference.semantic_key == requested.semantic_key) {
                            selected.push_back(TopoDS::Edge(owned.shape));
                            found = true;
                        }
                    }
                    if (!found) {
                        throw std::runtime_error(
                            "Fillet/Chamfer original edge is missing at this history boundary");
                    }
                }
                using Treatment = std::decay_t<decltype(treatment)>;
                if constexpr (std::is_same_v<Treatment, FilletRequest>) {
                    BRepFilletAPI_MakeFillet algorithm(result_shape);
                    for (const auto& edge : selected) algorithm.Add(size, edge);
                    algorithm.Build();
                    if (!algorithm.IsDone()) throw std::runtime_error("OCCT Fillet failed");
                    owned_faces = propagate_topology(
                        algorithm, owned_faces, std::vector<OwnedFace>{});
                    owned_edges = propagate_topology(
                        algorithm, owned_edges, std::vector<OwnedEdge>{});
                    owned_vertices = propagate_topology(
                        algorithm, owned_vertices, std::vector<OwnedVertex>{});
                    result_shape = algorithm.Shape();
                } else {
                    BRepFilletAPI_MakeChamfer algorithm(result_shape);
                    for (const auto& edge : selected) algorithm.Add(size, edge);
                    algorithm.Build();
                    if (!algorithm.IsDone()) throw std::runtime_error("OCCT Chamfer failed");
                    owned_faces = propagate_topology(
                        algorithm, owned_faces, std::vector<OwnedFace>{});
                    owned_edges = propagate_topology(
                        algorithm, owned_edges, std::vector<OwnedEdge>{});
                    owned_vertices = propagate_topology(
                        algorithm, owned_vertices, std::vector<OwnedVertex>{});
                    result_shape = algorithm.Shape();
                }
                boundaries.push_back(
                    make_result(result_shape, owned_faces, owned_edges, owned_vertices));
                boundaries.back().mesh.original_references = original_references;
                boundaries.back().source_fingerprint =
                    history_fingerprint(operations, boundaries.size());
            };
            if (const auto* fillet = std::get_if<FilletRequest>(&operation.primitive)) {
                apply_edge_treatment(*fillet);
                continue;
            }
            if (const auto* chamfer = std::get_if<ChamferRequest>(&operation.primitive)) {
                apply_edge_treatment(*chamfer);
                continue;
            }
            const bool imported_step = std::holds_alternative<StepRequest>(operation.primitive);
            const PrimitiveData operand = std::visit([&](const auto& primitive)
                -> PrimitiveData {
                using Request = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<Request, BoxRequest>) {
                    validate_box(primitive);
                    return make_box_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, CylinderRequest>) {
                    validate_cylinder(primitive);
                    return make_cylinder_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, SphereRequest>) {
                    validate_sphere(primitive);
                    return make_sphere_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, ConeRequest>) {
                    validate_cone(primitive);
                    return make_cone_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, PyramidRequest>) {
                    validate_pyramid(primitive);
                    return make_pyramid_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, WedgeRequest>) {
                    validate_wedge(primitive);
                    return make_wedge_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                    validate_extrusion(primitive);
                    if ((primitive.extent == ExtrusionRequest::Extent::UpToPlane ||
                         primitive.extent == ExtrusionRequest::Extent::UpToSurface) &&
                        !primitive.target_is_datum &&
                        std::none_of(owned_faces.begin(), owned_faces.end(),
                            [&](const auto& face) {
                                return face.reference.owner_id ==
                                           primitive.target_face.owner_id &&
                                       face.reference.semantic_key ==
                                           primitive.target_face.semantic_key;
                            })) {
                        throw std::runtime_error(
                            "Extrusion target face is missing at this history boundary");
                    }
                    std::optional<TopoDS_Face> exact_target;
                    if (primitive.extent == ExtrusionRequest::Extent::UpToSurface) {
                        std::vector<const OwnedFace*> matching_faces;
                        for (const auto& face : owned_faces) {
                            if (face.reference.owner_id ==
                                    primitive.target_face.owner_id &&
                                face.reference.semantic_key ==
                                    primitive.target_face.semantic_key) {
                                matching_faces.push_back(&face);
                            }
                        }
                        if (matching_faces.size() != 1) {
                            throw std::runtime_error(
                                "Exact Extrusion target surface is missing or ambiguous");
                        }
                        exact_target = TopoDS::Face(matching_faces.front()->shape);
                    }
                    return make_extrusion_data(
                        primitive, operation.owner_id, exact_target);
                } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                    validate_revolution(primitive);
                    return make_revolution_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, StepRequest>) {
                    return make_step_data(primitive, operation.owner_id, step_documents);
                } else {
                    throw std::logic_error("Edge treatment reached primitive builder");
                }
            }, operation.primitive);
            auto operand_result = make_result(
                operand.shape, operand.faces, operand.edges, operand.vertices, true);
            operand_result.mesh.axes = axes_for_operation(operation);
            append_original_reference_geometry(
                original_references, std::move(operand_result.mesh));
            if (result_shape.IsNull()) {
                result_shape = operand.shape;
                owned_faces = operand.faces;
                owned_edges = operand.edges;
                owned_vertices = operand.vertices;
            } else if (imported_step && operation.operation == BooleanOperation::Add) {
                TopoDS_Compound compound;
                BRep_Builder builder;
                builder.MakeCompound(compound);
                builder.Add(compound, result_shape);
                builder.Add(compound, operand.shape);
                result_shape = compound;
                owned_faces.insert(owned_faces.end(), operand.faces.begin(), operand.faces.end());
                owned_edges.insert(owned_edges.end(), operand.edges.begin(), operand.edges.end());
                owned_vertices.insert(
                    owned_vertices.end(), operand.vertices.begin(), operand.vertices.end());
            } else if (operation.operation == BooleanOperation::Add) {
                BRepAlgoAPI_Fuse algorithm(result_shape, operand.shape);
                algorithm.SetToFillHistory(true);
                algorithm.Build();
                if (!algorithm.IsDone()) throw std::runtime_error("OCCT fuse failed");
                owned_faces = propagate_topology(algorithm, owned_faces, operand.faces);
                owned_edges = propagate_topology(algorithm, owned_edges, operand.edges);
                owned_vertices = propagate_topology(
                    algorithm, owned_vertices, operand.vertices);
                result_shape = algorithm.Shape();
            } else {
                BRepAlgoAPI_Cut algorithm(result_shape, operand.shape);
                algorithm.SetToFillHistory(true);
                algorithm.Build();
                if (!algorithm.IsDone()) throw std::runtime_error("OCCT cut failed");
                owned_faces = propagate_topology(algorithm, owned_faces, operand.faces);
                owned_edges = propagate_topology(algorithm, owned_edges, operand.edges);
                owned_vertices = propagate_topology(
                    algorithm, owned_vertices, operand.vertices);
                result_shape = algorithm.Shape();
            }
            boundaries.push_back(
                make_result(result_shape, owned_faces, owned_edges, owned_vertices));
            boundaries.back().mesh.original_references = original_references;
            boundaries.back().source_fingerprint =
                history_fingerprint(operations, boundaries.size());
        }
        return boundaries;
    } catch (const Standard_Failure& failure) {
        throw std::runtime_error(failure.GetMessageString());
    }
}

}  // namespace zima::kernel
