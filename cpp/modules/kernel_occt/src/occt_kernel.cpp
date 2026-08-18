#include <zima/kernel/occt_kernel.hpp>

#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
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
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
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
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <array>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
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
        } else {
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

void validate_extrusion(const ExtrusionRequest& request) {
    const auto validate_profile = [](const auto& profile_variant) {
        std::visit([](const auto& profile) {
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
            } else {
                if (profile.curves.size() < 2) {
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
                        if (distance(exact_curve.start, exact_curve.end) <= 1.0e-12) {
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
    const double length = std::sqrt(
        request.direction.x * request.direction.x +
        request.direction.y * request.direction.y +
        request.direction.z * request.direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("Extrusion direction must be finite and non-zero");
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
        } else {
            BRepBuilderAPI_MakeWire curved_wire;
            for (const auto& curve : profile.curves) {
                const TopoDS_Edge edge = std::visit([](const auto& exact_curve) {
                    using Curve = std::decay_t<decltype(exact_curve)>;
                    if constexpr (std::is_same_v<Curve,
                                      ExtrusionRequest::LineCurve>) {
                        return BRepBuilderAPI_MakeEdge(
                            gp_Pnt(exact_curve.start.x, exact_curve.start.y,
                                   exact_curve.start.z),
                            gp_Pnt(exact_curve.end.x, exact_curve.end.y,
                                   exact_curve.end.z)).Edge();
                    } else {
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
    const ExtrusionRequest& request, const std::string& owner_id) {
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
    const TopoDS_Face face = face_builder.Face();
    if (!BRepCheck_Analyzer(face).IsValid()) {
        throw std::runtime_error("OCCT profile face is invalid");
    }
    BRepPrimAPI_MakePrism prism(face, gp_Vec(
        request.direction.x, request.direction.y, request.direction.z), true, true);
    prism.Build();
    if (!prism.IsDone() || !BRepCheck_Analyzer(prism.Shape()).IsValid()) {
        throw std::runtime_error("OCCT extrusion failed or produced an invalid solid");
    }
    PrimitiveData result{prism.Shape(), {}, {}, {}};
    result.faces.push_back({prism.FirstShape(), {owner_id, "profile_start"}});
    result.faces.push_back({prism.LastShape(), {owner_id, "profile_end"}});
    std::size_t edge_index{};
    for (const auto& wire : wires) {
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto index = std::to_string(edge_index++);
            const auto& generated = prism.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "side:" + index}});
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
    return result;
}

void validate_revolution(const RevolutionRequest& request) {
    ExtrusionRequest profile_request;
    profile_request.outer_profile = request.outer_profile;
    profile_request.inner_profiles = request.inner_profiles;
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
    std::size_t edge_index{};
    for (const auto& wire : wires) {
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto index = std::to_string(edge_index++);
            const auto& generated = revolution.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "side:" + index}});
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
    const std::vector<OwnedVertex>& owned_vertices) {
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
        const FaceReference reference =
            reference_for_shape<FaceReference>(face, owned_faces);
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
        const EdgeReference reference =
            reference_for_shape<EdgeReference>(edge, owned_edges);
        if (!reference.valid()) continue;
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
    for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
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
        std::vector<BodyResult> boundaries;
        boundaries.reserve(operations.size());
        for (const auto& operation : operations) {
            if (operation.owner_id.empty()) {
                throw std::invalid_argument("History operation owner ID is required");
            }
            const PrimitiveData operand = std::visit([&](const auto& primitive) {
                using Request = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<Request, BoxRequest>) {
                    validate_box(primitive);
                    return make_box_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, CylinderRequest>) {
                    validate_cylinder(primitive);
                    return make_cylinder_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                    validate_extrusion(primitive);
                    return make_extrusion_data(primitive, operation.owner_id);
                } else {
                    validate_revolution(primitive);
                    return make_revolution_data(primitive, operation.owner_id);
                }
            }, operation.primitive);
            if (result_shape.IsNull()) {
                result_shape = operand.shape;
                owned_faces = operand.faces;
                owned_edges = operand.edges;
                owned_vertices = operand.vertices;
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
            for (std::size_t index = 0; index < boundaries.size(); ++index) {
                auto axes = axes_for_operation(operations[index]);
                boundaries.back().mesh.axes.insert(
                    boundaries.back().mesh.axes.end(), axes.begin(), axes.end());
            }
            boundaries.back().source_fingerprint =
                history_fingerprint(operations, boundaries.size());
        }
        return boundaries;
    } catch (const Standard_Failure& failure) {
        throw std::runtime_error(failure.GetMessageString());
    }
}

}  // namespace zima::kernel
