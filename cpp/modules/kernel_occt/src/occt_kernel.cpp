#include <zima/kernel/occt_kernel.hpp>

#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
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
#include <BRepClass_FaceClassifier.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepTools_History.hxx>
#include <BRep_Tool.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#include <TransferBRep.hxx>
#include <Interface_InterfaceModel.hxx>
#include <StepShape_FaceSurface.hxx>
#include <StepShape_EdgeCurve.hxx>
#include <StepShape_VertexPoint.hxx>
#include <StlAPI_Writer.hxx>
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
#include <Geom_Surface.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <bit>
#include <cmath>
#include <array>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <memory>

namespace zima::kernel {

namespace {

constexpr std::size_t kLiveShapeCacheLimit = 256;

gp_Trsf primitive_transform(const Vec3& translation, const Vec3& rotation_degrees);

TopoDS_Shape read_kernel_shape(const BodyResult& body) {
    if (body.kernel_shape.empty()) {
        throw std::invalid_argument("Calculated solid snapshot is missing");
    }
    BRep_Builder builder;
    TopoDS_Shape shape;
    std::istringstream stream(body.kernel_shape);
    BRepTools::Read(shape, stream, builder);
    if (shape.IsNull()) {
        throw std::runtime_error("Calculated solid snapshot is invalid");
    }
    return shape;
}

TopoDS_Compound placed_compound(const std::vector<PlacedBody>& bodies) {
    if (bodies.empty()) throw std::invalid_argument("Export has no visible bodies");
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& placed : bodies) {
        const auto shape = read_kernel_shape(placed.body);
        BRepBuilderAPI_Transform transformed(shape,
            primitive_transform(placed.translation, placed.rotation_degrees), true);
        transformed.Build();
        if (!transformed.IsDone() || transformed.Shape().IsNull()) {
            throw std::runtime_error("Export body placement failed");
        }
        builder.Add(compound, transformed.Shape());
    }
    return compound;
}

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
    std::vector<StepRequest::TopologyIdentity> imported_step_topology;
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

template <typename Owned>
std::vector<Owned> propagate_topology(
    const Handle(BRepTools_History)& history,
    const std::vector<Owned>& existing) {
    if (history.IsNull()) return existing;
    std::vector<Owned> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape,
                                   const auto& reference) {
        if (std::none_of(propagated.begin(), propagated.end(),
                [&](const auto& value) {
                    return value.shape.IsSame(shape) &&
                        value.reference == reference;
                })) {
            propagated.push_back({shape, reference});
        }
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &history->Modified(source.shape),
                &history->Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) continue;
                append_unique(iterator.Value(), source.reference);
                has_descendant = true;
            }
        }
        if (!has_descendant && !history->IsRemoved(source.shape)) {
            append_unique(source.shape, source.reference);
        }
    }
    return propagated;
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

std::vector<ViewerAxis> axes_for_operation(
    const HistoryOperation& operation, const TopoDS_Shape& calculated_operand) {
    const auto fitted_axis = [&](Vec3 point, Vec3 direction,
            std::string semantic_key, double fallback_length) {
        const double magnitude = std::hypot(
            std::hypot(direction.x, direction.y), direction.z);
        direction = {direction.x / magnitude, direction.y / magnitude,
                     direction.z / magnitude};
        if (!calculated_operand.IsNull()) {
            Bnd_Box bounds;
            BRepBndLib::Add(calculated_operand, bounds);
            if (!bounds.IsVoid()) {
                Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                double minimum = std::numeric_limits<double>::infinity();
                double maximum = -std::numeric_limits<double>::infinity();
                for (unsigned mask = 0; mask < 8; ++mask) {
                    const Vec3 corner{mask & 1 ? xmax : xmin,
                        mask & 2 ? ymax : ymin, mask & 4 ? zmax : zmin};
                    const double projection = (corner.x-point.x)*direction.x +
                        (corner.y-point.y)*direction.y +
                        (corner.z-point.z)*direction.z;
                    minimum = std::min(minimum, projection);
                    maximum = std::max(maximum, projection);
                }
                const double diagonal = std::hypot(
                    std::hypot(xmax-xmin, ymax-ymin), zmax-zmin);
                const double margin = std::max(1.0, diagonal * 1.0e-4);
                const double middle = (minimum + maximum) * 0.5;
                point = {point.x + direction.x * middle,
                         point.y + direction.y * middle,
                         point.z + direction.z * middle};
                fallback_length = maximum - minimum + 2.0 * margin;
            }
        }
        return ViewerAxis{point, direction, std::max(1.0, fallback_length),
            {operation.owner_id, std::move(semantic_key)}};
    };
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
            const auto transformed = transformed_axis(operation.owner_id,
                "axis:primary", {0.0, 0.0, 1.0}, primitive.height, transform);
            return std::vector<ViewerAxis>{fitted_axis(transformed.point,
                transformed.direction, "axis:primary", primitive.height)};
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
            const auto transformed = transformed_axis(operation.owner_id,
                "axis:primary", {0.0, 0.0, 1.0}, primitive.height, transform);
            return std::vector<ViewerAxis>{fitted_axis(transformed.point,
                transformed.direction, "axis:primary", primitive.height)};
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
            const Vec3 direction{primitive.direction.x / length,
                primitive.direction.y / length, primitive.direction.z / length};
            std::vector<Vec3> centers;
            const auto append_center = [&](const auto& profile) {
                using Profile = std::decay_t<decltype(profile)>;
                if constexpr (std::is_same_v<Profile,
                                  ExtrusionRequest::CircleProfile> ||
                              std::is_same_v<Profile,
                                  ExtrusionRequest::EllipseProfile>) {
                    if (std::none_of(centers.begin(), centers.end(),
                            [&](const auto& center) {
                                return std::hypot(std::hypot(
                                    center.x-profile.center.x,
                                    center.y-profile.center.y),
                                    center.z-profile.center.z) <= 1.0e-7;
                            })) centers.push_back(profile.center);
                }
            };
            const auto append_loop = [&](const ExtrusionRequest::ProfileLoop& loop) {
                std::visit(append_center, loop);
            };
            append_loop(primitive.outer_profile);
            for (const auto& loop : primitive.inner_profiles) append_loop(loop);
            for (const auto& region : primitive.additional_profile_regions) {
                append_loop(region.outer_profile);
                for (const auto& loop : region.inner_profiles) append_loop(loop);
            }
            std::vector<ViewerAxis> axes;
            for (std::size_t index = 0; index < centers.size(); ++index) {
                axes.push_back(fitted_axis(centers[index], direction,
                    index == 0 ? "axis:primary"
                               : "axis:profile:" + std::to_string(index + 1),
                    length));
            }
            return axes;
        } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
            const double length = std::max(10.0, std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z));
            const double direction_length = std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z);
            return std::vector<ViewerAxis>{fitted_axis(primitive.axis_point,
                {primitive.axis_direction.x / direction_length,
                 primitive.axis_direction.y / direction_length,
                 primitive.axis_direction.z / direction_length},
                "axis:primary", length)};
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
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const auto transformed = transformer.ModifiedShape(explorer.Current());
        if (!transformed.IsNull()) result.faces.push_back({transformed, {owner_id, "surface"}});
    }
    // Sphere seams and poles are kernel parameterization artifacts, not ZIMA
    // topology entities, so they intentionally have no persistent references.
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
        const auto edge = TopoDS::Edge(explorer.Current());
        TopoDS_Vertex first;
        TopoDS_Vertex last;
        TopExp::Vertices(edge, first, last);
        const double first_z = BRep_Tool::Pnt(first).Z();
        const double last_z = BRep_Tool::Pnt(last).Z();
        if (std::abs(first_z - last_z) > 1.0e-7) continue;
        const auto transformed = transformer.ModifiedShape(edge);
        if (!transformed.IsNull()) result.edges.push_back({transformed,
            {owner_id, std::abs(first_z) < 1.0e-7
                ? "circle:z_min" : "circle:z_max"}});
    }
    return result;
}

PrimitiveData make_polyhedral_data(
    const TopoDS_Shape& unplaced, const gp_Trsf& transform,
    const std::string& owner_id, std::string_view kind) {
    BRepBuilderAPI_Transform transformer(unplaced, transform, true);
    PrimitiveData result{transformer.Shape(), {}, {}, {}};
    TopTools_IndexedMapOfShape vertex_map;
    TopExp::MapShapes(unplaced, TopAbs_VERTEX, vertex_map);
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double min_z = min_x;
    double max_x = -min_x;
    double max_y = max_x;
    double max_z = max_x;
    for (int index = 1; index <= vertex_map.Extent(); ++index) {
        const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertex_map(index)));
        min_x = std::min(min_x, point.X()); max_x = std::max(max_x, point.X());
        min_y = std::min(min_y, point.Y()); max_y = std::max(max_y, point.Y());
        min_z = std::min(min_z, point.Z()); max_z = std::max(max_z, point.Z());
    }
    const auto coordinate_role = [](double value, double minimum,
                                    double maximum) {
        constexpr double tolerance = 1.0e-7;
        if (std::abs(value - minimum) <= tolerance) return std::string{"min"};
        if (std::abs(value - maximum) <= tolerance) return std::string{"max"};
        return std::string{"mid"};
    };
    std::vector<std::pair<TopoDS_Vertex, std::string>> vertex_roles;
    for (int index = 1; index <= vertex_map.Extent(); ++index) {
        const auto vertex = TopoDS::Vertex(vertex_map(index));
        const auto point = BRep_Tool::Pnt(vertex);
        auto role = std::string{"vertex:x_"} +
            coordinate_role(point.X(), min_x, max_x) + ":y_" +
            coordinate_role(point.Y(), min_y, max_y) + ":z_" +
            coordinate_role(point.Z(), min_z, max_z);
        vertex_roles.emplace_back(vertex, role);
        const auto transformed = transformer.ModifiedShape(vertex);
        if (!transformed.IsNull()) {
            result.vertices.push_back({transformed, {owner_id, std::move(role)}});
        }
    }
    const auto vertex_role = [&](const TopoDS_Vertex& vertex) {
        const auto found = std::find_if(vertex_roles.begin(), vertex_roles.end(),
            [&](const auto& entry) { return entry.first.IsSame(vertex); });
        return found == vertex_roles.end() ? std::string{} : found->second;
    };
    TopTools_IndexedMapOfShape edge_map;
    TopExp::MapShapes(unplaced, TopAbs_EDGE, edge_map);
    for (int index = 1; index <= edge_map.Extent(); ++index) {
        const auto edge = TopoDS::Edge(edge_map(index));
        TopoDS_Vertex first;
        TopoDS_Vertex second;
        TopExp::Vertices(edge, first, second, true);
        auto first_role = vertex_role(first);
        auto second_role = vertex_role(second);
        if (second_role < first_role) std::swap(first_role, second_role);
        const auto transformed = transformer.ModifiedShape(edge);
        if (!transformed.IsNull() && !first_role.empty() && !second_role.empty()) {
            result.edges.push_back({transformed,
                {owner_id, "edge:" + first_role.substr(7) + "--" +
                    second_role.substr(7)}});
        }
    }
    // These semantic roles are defined by the primitive's parametric frame,
    // never by OCCT traversal position. OCCT is used only to locate the
    // runtime face matching each already-defined ZIMA role.
    for (TopExp_Explorer explorer(unplaced, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        const auto face = TopoDS::Face(explorer.Current());
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        const auto center = properties.CentreOfMass();
        std::string role;
        constexpr double tolerance = 1.0e-7;
        if (kind == "pyramid") {
            if (std::abs(center.Z()) <= tolerance) role = "face:base";
            else if (std::abs(center.X()) >= std::abs(center.Y())) {
                role = center.X() < 0.0 ? "face:x_min" : "face:x_max";
            } else {
                role = center.Y() < 0.0 ? "face:y_min" : "face:y_max";
            }
        } else {
            // A centered wedge has two end caps at y extrema, a bottom at
            // z=0, one vertical x-end and one sloped roof. Classify by the
            // defining parametric coordinates; no enumeration index enters
            // the persisted identity.
            BRepAdaptor_Surface surface(face);
            const auto surface_type = surface.GetType();
            if (std::abs(center.Z()) <= tolerance) role = "face:bottom";
            else if (surface_type == GeomAbs_Plane) {
                const auto plane = surface.Plane();
                const auto normal = plane.Axis().Direction();
                const double ax = std::abs(normal.X());
                const double ay = std::abs(normal.Y());
                const double az = std::abs(normal.Z());
                if (ay >= ax && ay >= az) {
                    role = normal.Y() < 0.0 ? "face:y_min" : "face:y_max";
                } else if (ax >= ay && ax >= az) {
                    role = normal.X() < 0.0 ? "face:x_min" : "face:x_max";
                } else {
                    role = "face:slope";
                }
            } else {
                role = "face:slope";
            }
        }
        const auto transformed = transformer.ModifiedShape(face);
        if (!transformed.IsNull()) {
            result.faces.push_back({transformed, {owner_id, std::move(role)}});
        }
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
        primitive_transform(request.translation, request.rotation_degrees),
        owner_id, "pyramid");
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
        primitive_transform(request.translation, request.rotation_degrees),
        owner_id, "wedge");
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
                        if (exact_curve.interpolating) {
                            Handle(TColgp_HArray1OfPnt) points =
                                new TColgp_HArray1OfPnt(1, pole_count);
                            for (Standard_Integer index = 1; index <= pole_count; ++index) {
                                const auto& point = exact_curve.control_points[
                                    static_cast<std::size_t>(index - 1)];
                                points->SetValue(index,
                                    gp_Pnt(point.x, point.y, point.z));
                            }
                            GeomAPI_Interpolate interpolation(
                                points, exact_curve.periodic, 1.0e-9);
                            interpolation.Perform();
                            if (!interpolation.IsDone()) {
                                throw std::runtime_error(
                                    "OCCT profile interpolating spline failed");
                            }
                            BRepBuilderAPI_MakeEdge edge(interpolation.Curve());
                            if (!edge.IsDone()) {
                                throw std::runtime_error(
                                    "OCCT profile interpolating spline edge failed");
                            }
                            return edge.Edge();
                        }
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
    const std::optional<TopoDS_Face>& exact_target = std::nullopt,
    double through_all_forward_span = 2'000'000.0,
    double through_all_reverse_span = 2'000'000.0) {
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
    if (std::abs(request.start_offset) > 1.0e-12) {
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(unit.x * request.start_offset,
                                    unit.y * request.start_offset,
                                    unit.z * request.start_offset));
        for (auto& wire : wires) {
            wire = TopoDS::Wire(BRepBuilderAPI_Transform(wire, shift, true).Shape());
        }
        BRepBuilderAPI_MakeFace shifted_face(wires.front(), true);
        for (std::size_t index = 1; index < wires.size(); ++index) {
            shifted_face.Add(wires[index]);
        }
        if (!shifted_face.IsDone()) {
            throw std::runtime_error("OCCT extrusion start shift failed");
        }
        face = shifted_face.Face();
    }
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
        const double forward_span = std::max(1.0, through_all_forward_span);
        const double reverse_span = std::max(1.0, through_all_reverse_span);
        gp_Trsf shift;
        // The wires already contain start_offset.  Through-all is bounded
        // relative to the persisted Sketch plane, so cancel that earlier
        // shift before moving to the finite reverse endpoint.
        const double offset = -(reverse_span + request.start_offset);
        shift.SetTranslation(gp_Vec(unit.x * offset,
                                    unit.y * offset,
                                    unit.z * offset));
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
        prism_direction = {unit.x * (forward_span + reverse_span),
                           unit.y * (forward_span + reverse_span),
                           unit.z * (forward_span + reverse_span)};
    }
    BRepPrimAPI_MakePrism prism(face, gp_Vec(
        prism_direction.x, prism_direction.y, prism_direction.z), true, true);
    prism.Build();
    if (!prism.IsDone() || !BRepCheck_Analyzer(prism.Shape()).IsValid()) {
        throw std::runtime_error("OCCT extrusion failed or produced an invalid solid");
    }
    PrimitiveData result{prism.Shape(), {}, {}, {}};
    const std::string first_role = request.first_cap_is_start ? "start" : "end";
    const std::string last_role = request.first_cap_is_start ? "end" : "start";
    result.faces.push_back({prism.FirstShape(), {owner_id, first_role}});
    result.faces.push_back({prism.LastShape(), {owner_id, last_role}});
    std::vector<std::vector<std::string>> edge_sources{
        request.outer_edge_source_ids};
    edge_sources.insert(edge_sources.end(), request.inner_edge_source_ids.begin(),
                        request.inner_edge_source_ids.end());
    std::vector<std::vector<std::string>> vertex_sources{
        request.outer_vertex_source_ids};
    vertex_sources.insert(vertex_sources.end(),
                          request.inner_vertex_source_ids.begin(),
                          request.inner_vertex_source_ids.end());
    if (edge_sources.size() != wires.size() || vertex_sources.size() != wires.size()) {
        throw std::runtime_error("Extrusion profile provenance group mismatch");
    }
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        const auto& wire = wires[wire_index];
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next(), ++boundary_edge) {
            if (boundary_edge >= edge_sources[wire_index].size()) {
                throw std::runtime_error("Extrusion edge provenance mismatch");
            }
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto& curve_id = edge_sources[wire_index][boundary_edge];
            if (curve_id.empty()) {
                throw std::runtime_error("Extrusion curve provenance is empty");
            }
            const auto& generated = prism.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "generated:" + curve_id}});
                }
            }
            const auto first = prism.FirstShape(edge);
            const auto last = prism.LastShape(edge);
            if (!first.IsNull()) {
                result.edges.push_back(
                    {first, {owner_id, first_role + ":" + curve_id}});
            }
            if (!last.IsNull()) {
                result.edges.push_back(
                    {last, {owner_id, last_role + ":" + curve_id}});
            }
            if (vertex_sources[wire_index].empty()) continue;
            if (boundary_edge >= vertex_sources[wire_index].size()) {
                throw std::runtime_error("Extrusion point provenance mismatch");
            }
            const auto& point_id = vertex_sources[wire_index][boundary_edge];
            const TopoDS_Vertex vertex = TopExp::FirstVertex(edge, true);
            const auto first_vertex = prism.FirstShape(vertex);
            const auto last_vertex = prism.LastShape(vertex);
            if (!first_vertex.IsNull()) {
                result.vertices.push_back(
                    {first_vertex, {owner_id, first_role + ":" + point_id}});
            }
            if (!last_vertex.IsNull()) {
                result.vertices.push_back(
                    {last_vertex, {owner_id, last_role + ":" + point_id}});
            }
            const auto& generated_from_vertex = prism.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated_from_vertex);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id, "generated:" + point_id}});
                }
            }
        }
        if (boundary_edge != edge_sources[wire_index].size() ||
            (!vertex_sources[wire_index].empty() &&
             boundary_edge != vertex_sources[wire_index].size())) {
            throw std::runtime_error("Extrusion profile provenance count mismatch");
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
        // Clipping may create new result-body topology.  It is deliberately not
        // exposed as a stable reference: no persisted ZIMA source owns it.
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
            additional_request.outer_edge_source_ids =
                request.additional_profile_regions[index].outer_edge_source_ids;
            additional_request.inner_edge_source_ids =
                request.additional_profile_regions[index].inner_edge_source_ids;
            additional_request.outer_vertex_source_ids =
                request.additional_profile_regions[index].outer_vertex_source_ids;
            additional_request.inner_vertex_source_ids =
                request.additional_profile_regions[index].inner_vertex_source_ids;
            additional_request.additional_profile_regions.clear();
            auto additional = make_extrusion_data(
                additional_request, owner_id, exact_target,
                through_all_forward_span, through_all_reverse_span);
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

std::string step_shape_locator(const TopoDS_Shape& shape) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append_integer = [&](std::uint64_t value) {
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            hash ^= static_cast<unsigned char>(value >> (byte * 8));
            hash *= 1099511628211ULL;
        }
    };
    const auto append_real = [&](double value) {
        append_integer(std::bit_cast<std::uint64_t>(value));
    };
    const auto append_point = [&](const gp_Pnt& point) {
        append_real(point.X());
        append_real(point.Y());
        append_real(point.Z());
    };
    append_integer(static_cast<std::uint64_t>(shape.ShapeType()));
    append_integer(static_cast<std::uint64_t>(shape.Orientation()));
    const gp_Trsf placement = shape.Location().Transformation();
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 4; ++column) {
            append_real(placement.Value(row, column));
        }
    }
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (!bounds.IsVoid()) {
        Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        for (const double value : {xmin, ymin, zmin, xmax, ymax, zmax}) {
            append_real(value);
        }
    }
    if (shape.ShapeType() == TopAbs_VERTEX) {
        append_point(BRep_Tool::Pnt(TopoDS::Vertex(shape)));
    } else if (shape.ShapeType() == TopAbs_EDGE) {
        const TopoDS_Edge edge = TopoDS::Edge(shape);
        BRepAdaptor_Curve curve(edge);
        append_integer(static_cast<std::uint64_t>(curve.GetType()));
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        append_real(first);
        append_real(last);
        for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            append_point(curve.Value(first + (last - first) * fraction));
        }
        GProp_GProps properties;
        BRepGProp::LinearProperties(edge, properties);
        append_real(properties.Mass());
        append_point(properties.CentreOfMass());
    } else if (shape.ShapeType() == TopAbs_FACE) {
        const TopoDS_Face face = TopoDS::Face(shape);
        BRepAdaptor_Surface surface(face);
        append_integer(static_cast<std::uint64_t>(surface.GetType()));
        Standard_Real u_min{}, u_max{}, v_min{}, v_max{};
        BRepTools::UVBounds(face, u_min, u_max, v_min, v_max);
        for (const double value : {u_min, u_max, v_min, v_max}) {
            append_real(value);
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        append_real(properties.Mass());
        append_point(properties.CentreOfMass());
        const gp_Mat inertia = properties.MatrixOfInertia();
        for (int row = 1; row <= 3; ++row) {
            for (int column = 1; column <= 3; ++column) {
                append_real(inertia.Value(row, column));
            }
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

template <typename Owned>
void capture_step_topology_kind(
    const STEPControl_Reader& reader, const TopoDS_Shape& imported,
    TopAbs_ShapeEnum shape_kind, StepRequest::TopologyIdentity::Kind identity_kind,
    const Handle(Standard_Type)& source_type, const char* semantic_prefix,
    const std::string& owner_id,
    std::vector<Owned>& owned,
    std::vector<StepRequest::TopologyIdentity>& identities) {
    const auto transfer = reader.WS()->TransferReader()->TransientProcess();
    const auto model = reader.Model();
    struct Candidate {
        TopoDS_Shape shape;
        std::string semantic_key;
        std::string locator;
    };
    TopTools_IndexedMapOfShape runtime_shapes;
    for (TopExp_Explorer explorer(imported, shape_kind);
         explorer.More(); explorer.Next()) {
        const TopoDS_Shape shape = explorer.Current();
        if (!runtime_shapes.Contains(shape)) runtime_shapes.Add(shape);
    }
    std::map<int, std::vector<Candidate>> by_runtime_shape;
    for (int index = 1; index <= model->NbEntities(); ++index) {
        const auto entity = model->Value(index);
        if (entity.IsNull() || !entity->IsKind(source_type)) continue;
        const TopoDS_Shape shape = TransferBRep::ShapeResult(transfer, entity);
        if (shape.IsNull()) continue;
        if (shape.ShapeType() != shape_kind) continue;
        std::vector<int> runtime_indices;
        const int exact_runtime_index = runtime_shapes.FindIndex(shape);
        if (exact_runtime_index != 0) {
            runtime_indices.push_back(exact_runtime_index);
        } else {
            // STEP assemblies transfer one definition subshape and place the
            // same TShape into one or more product occurrences. IsPartner()
            // follows that source relation without using traversal position.
            for (int runtime_index = 1;
                 runtime_index <= runtime_shapes.Size(); ++runtime_index) {
                if (runtime_shapes.FindKey(runtime_index).IsPartner(shape)) {
                    runtime_indices.push_back(runtime_index);
                }
            }
        }
        if (runtime_indices.empty()) continue;
        const int source_entity_number = model->Number(entity);
        if (source_entity_number <= 0) continue;
        const std::string source_label =
            "#" + std::to_string(source_entity_number);
        for (const int runtime_index : runtime_indices) {
            const TopoDS_Shape runtime_shape =
                runtime_shapes.FindKey(runtime_index);
            const std::string locator = step_shape_locator(runtime_shape);
            std::string semantic_key =
                std::string(semantic_prefix) + source_label;
            if (runtime_indices.size() > 1) {
                // One STEP definition can appear many times in an assembly.
                // The parent remains the source STEP entity; the occurrence
                // role comes from its persisted placement/shape locator.
                semantic_key += "@occ:" + locator;
            }
            by_runtime_shape[runtime_index].push_back({runtime_shape,
                std::move(semantic_key), locator});
        }
    }
    std::set<std::string> semantic_keys;
    for (auto& [runtime_index, candidates] : by_runtime_shape) {
        static_cast<void>(runtime_index);
        // A runtime subshape must come from exactly one canonical STEP
        // topological entity. Ambiguous transfer results remain display-only.
        if (candidates.size() != 1) continue;
        auto& candidate = candidates.front();
        if (!semantic_keys.insert(candidate.semantic_key).second) continue;
        owned.push_back({candidate.shape,
            {owner_id, candidate.semantic_key, {}}});
        identities.push_back({identity_kind, std::move(candidate.semantic_key),
            std::move(candidate.locator)});
    }
}

void capture_step_topology(const STEPControl_Reader& reader,
    const TopoDS_Shape& imported, const std::string& owner_id,
    PrimitiveData& result) {
    using Kind = StepRequest::TopologyIdentity::Kind;
    capture_step_topology_kind(reader, imported, TopAbs_FACE, Kind::Face,
        STANDARD_TYPE(StepShape_FaceSurface), "step:face:", owner_id,
        result.faces, result.imported_step_topology);
    capture_step_topology_kind(reader, imported, TopAbs_EDGE, Kind::Edge,
        STANDARD_TYPE(StepShape_EdgeCurve), "step:edge:", owner_id,
        result.edges, result.imported_step_topology);
    capture_step_topology_kind(reader, imported, TopAbs_VERTEX, Kind::Vertex,
        STANDARD_TYPE(StepShape_VertexPoint), "step:vertex:", owner_id,
        result.vertices, result.imported_step_topology);
}

template <typename Owned, typename Reference>
void restore_step_topology_kind(const TopoDS_Shape& imported,
    TopAbs_ShapeEnum shape_kind, StepRequest::TopologyIdentity::Kind identity_kind,
    const std::vector<StepRequest::TopologyIdentity>& identities,
    const std::string& owner_id, std::vector<Owned>& owned) {
    std::unordered_map<std::string, std::vector<TopoDS_Shape>> by_locator;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(imported, shape_kind);
         explorer.More(); explorer.Next()) {
        const TopoDS_Shape shape = explorer.Current();
        if (visited.Contains(shape)) continue;
        visited.Add(shape);
        by_locator[step_shape_locator(shape)].push_back(shape);
    }
    for (const auto& identity : identities) {
        if (identity.kind != identity_kind) continue;
        const auto found = by_locator.find(identity.shape_locator);
        if (found == by_locator.end() || found->second.size() != 1) continue;
        owned.push_back({found->second.front(),
            Reference{owner_id, identity.semantic_key, {}}});
    }
}

void restore_step_topology(const StepRequest& request,
    const TopoDS_Shape& imported, const std::string& owner_id,
    PrimitiveData& result) {
    using Kind = StepRequest::TopologyIdentity::Kind;
    restore_step_topology_kind<OwnedFace, FaceReference>(imported, TopAbs_FACE,
        Kind::Face, request.topology, owner_id, result.faces);
    restore_step_topology_kind<OwnedEdge, EdgeReference>(imported, TopAbs_EDGE,
        Kind::Edge, request.topology, owner_id, result.edges);
    restore_step_topology_kind<OwnedVertex, VertexReference>(imported, TopAbs_VERTEX,
        Kind::Vertex, request.topology, owner_id, result.vertices);
}

struct StepDocumentCache {
    Handle(TDocStd_Document) document;
    std::unique_ptr<STEPCAFControl_Reader> reader;
};

PrimitiveData make_step_data(
    const StepRequest& request, const std::string& owner_id,
    std::unordered_map<std::string, StepDocumentCache>& documents) {
    PrimitiveData result;
    if (request.frozen_brep && !request.frozen_brep->empty()) {
        std::istringstream stream(*request.frozen_brep);
        BRep_Builder builder;
        BRepTools::Read(result.shape, stream, builder);
        if (result.shape.IsNull()) {
            throw std::runtime_error("Frozen STEP body is invalid");
        }
        restore_step_topology(request, result.shape, owner_id, result);
        result.imported_step_topology = request.topology;
    } else if (request.source_path.empty()) {
        throw std::invalid_argument("STEP source path is empty");
    } else if (request.component_path.empty()) {
        STEPControl_Reader reader;
        if (reader.ReadFile(request.source_path.c_str()) != IFSelect_RetDone ||
            reader.TransferRoots() == 0) {
            throw std::runtime_error("OCCT STEP import failed");
        }
        result.shape = reader.OneShape();
        capture_step_topology(reader, result.shape, owner_id, result);
    } else {
        auto& cached = documents[request.source_path];
        if (cached.document.IsNull()) {
            XCAFApp_Application::GetApplication()->NewDocument(
                "BinXCAF", cached.document);
            cached.reader = std::make_unique<STEPCAFControl_Reader>();
            if (cached.reader->ReadFile(request.source_path.c_str()) !=
                    IFSelect_RetDone ||
                !cached.reader->Transfer(cached.document)) {
                throw std::runtime_error("OCCT STEP product structure import failed");
            }
        }
        TDF_Label definition;
        TDF_Tool::Label(
            cached.document->GetData(), request.component_path.c_str(), definition, false);
        result.shape = definition.IsNull() ? TopoDS_Shape{}
            : XCAFDoc_DocumentTool::ShapeTool(cached.document->Main())->GetShape(definition);
        if (result.shape.IsNull()) throw std::runtime_error("STEP component is missing");
        capture_step_topology(
            cached.reader->Reader(), result.shape, owner_id, result);
    }
    if (result.shape.IsNull() || !BRepCheck_Analyzer(result.shape).IsValid()) {
        throw std::runtime_error("STEP did not produce a valid shape");
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
        axis_length <= 1.0e-12 || !std::isfinite(request.start_angle_degrees) ||
        !std::isfinite(request.angle_degrees) ||
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
    const gp_Ax1 axis(gp_Pnt(request.axis_point.x, request.axis_point.y,
                            request.axis_point.z),
                      gp_Dir(request.axis_direction.x, request.axis_direction.y,
                             request.axis_direction.z));
    TopoDS_Face face = face_builder.Face();
    if (std::abs(request.start_angle_degrees) > 1.0e-12) {
        gp_Trsf rotation;
        rotation.SetRotation(axis,
            request.start_angle_degrees * std::numbers::pi / 180.0);
        for (auto& wire : wires) {
            wire = TopoDS::Wire(
                BRepBuilderAPI_Transform(wire, rotation, true).Shape());
        }
        BRepBuilderAPI_MakeFace rotated_face(wires.front(), true);
        for (std::size_t index = 1; index < wires.size(); ++index) {
            rotated_face.Add(wires[index]);
        }
        if (!rotated_face.IsDone()) {
            throw std::runtime_error("OCCT Revolution start rotation failed");
        }
        face = rotated_face.Face();
    }
    BRepPrimAPI_MakeRevol revolution(
        face, axis,
        request.angle_degrees * std::numbers::pi / 180.0, true);
    revolution.Build();
    if (!revolution.IsDone() ||
        !BRepCheck_Analyzer(revolution.Shape()).IsValid()) {
        throw std::runtime_error("OCCT Revolution failed or produced an invalid solid");
    }
    PrimitiveData result{revolution.Shape(), {}, {}, {}};
    const std::string first_role = request.first_cap_is_start ? "start" : "end";
    const std::string last_role = request.first_cap_is_start ? "end" : "start";
    if (request.angle_degrees < 360.0 - 1.0e-9) {
        result.faces.push_back(
            {revolution.FirstShape(), {owner_id, first_role}});
        result.faces.push_back(
            {revolution.LastShape(), {owner_id, last_role}});
    }
    std::vector<std::vector<std::string>> edge_sources{
        request.outer_edge_source_ids};
    edge_sources.insert(edge_sources.end(), request.inner_edge_source_ids.begin(),
                        request.inner_edge_source_ids.end());
    std::vector<std::vector<std::string>> vertex_sources{
        request.outer_vertex_source_ids};
    vertex_sources.insert(vertex_sources.end(),
                          request.inner_vertex_source_ids.begin(),
                          request.inner_vertex_source_ids.end());
    if (edge_sources.size() != wires.size() || vertex_sources.size() != wires.size()) {
        throw std::runtime_error("Revolution profile provenance group mismatch");
    }
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        const auto& wire = wires[wire_index];
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
             explorer.More(); explorer.Next(), ++boundary_edge) {
            if (boundary_edge >= edge_sources[wire_index].size()) {
                throw std::runtime_error("Revolution edge provenance mismatch");
            }
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            const auto& curve_id = edge_sources[wire_index][boundary_edge];
            const auto& generated = revolution.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "generated:" + curve_id}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(edge);
                const auto last = revolution.LastShape(edge);
                if (!first.IsNull()) {
                    result.edges.push_back(
                        {first, {owner_id, first_role + ":" + curve_id}});
                }
                if (!last.IsNull()) {
                    result.edges.push_back(
                        {last, {owner_id, last_role + ":" + curve_id}});
                }
            }
            if (vertex_sources[wire_index].empty()) continue;
            if (boundary_edge >= vertex_sources[wire_index].size()) {
                throw std::runtime_error("Revolution point provenance mismatch");
            }
            const auto& point_id = vertex_sources[wire_index][boundary_edge];
            const TopoDS_Vertex vertex = TopExp::FirstVertex(edge, true);
            const auto& generated_from_vertex = revolution.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated_from_vertex);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id, "generated:" + point_id}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(vertex);
                const auto last = revolution.LastShape(vertex);
                if (!first.IsNull()) {
                    result.vertices.push_back(
                        {first, {owner_id, first_role + ":" + point_id}});
                }
                if (!last.IsNull()) {
                    result.vertices.push_back(
                        {last, {owner_id, last_role + ":" + point_id}});
                }
            }
        }
        if (boundary_edge != edge_sources[wire_index].size() ||
            (!vertex_sources[wire_index].empty() &&
             boundary_edge != vertex_sources[wire_index].size())) {
            throw std::runtime_error("Revolution profile provenance count mismatch");
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
            additional_request.outer_edge_source_ids =
                request.additional_profile_regions[index].outer_edge_source_ids;
            additional_request.inner_edge_source_ids =
                request.additional_profile_regions[index].inner_edge_source_ids;
            additional_request.outer_vertex_source_ids =
                request.additional_profile_regions[index].outer_vertex_source_ids;
            additional_request.inner_vertex_source_ids =
                request.additional_profile_regions[index].inner_vertex_source_ids;
            additional_request.additional_profile_regions.clear();
            auto additional = make_revolution_data(additional_request, owner_id);
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
    const auto append_unique = [&](const TopoDS_Shape& shape,
                                   const auto& reference) {
        if (std::none_of(propagated.begin(), propagated.end(),
                [&](const auto& value) {
                    return value.shape.IsSame(shape) &&
                        value.reference == reference;
                })) {
            propagated.push_back({shape, reference});
        }
    };
    auto propagate_one = [&](const Owned& source) {
        bool has_descendant = false;
        for (const auto* list : {
                &algorithm.Modified(source.shape), &algorithm.Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) continue;
                append_unique(iterator.Value(), source.reference);
                has_descendant = true;
            }
        }
        if (!has_descendant && !algorithm.IsDeleted(source.shape)) {
            append_unique(source.shape, source.reference);
        }
    };
    for (const auto& source : existing) propagate_one(source);
    for (const auto& source : operand) propagate_one(source);
    return propagated;
}

template <typename Reference, typename Owned>
class TopologyReferenceIndex {
public:
    explicit TopologyReferenceIndex(const std::vector<Owned>& owned_shapes) {
        references_.resize(1);
        ambiguous_.resize(1);
        for (const auto& owned : owned_shapes) {
            int index = shapes_.FindIndex(owned.shape);
            if (index == 0) {
                index = shapes_.Add(owned.shape);
                references_.resize(static_cast<std::size_t>(index) + 1);
                ambiguous_.resize(static_cast<std::size_t>(index) + 1);
                references_[static_cast<std::size_t>(index)] = owned.reference;
            } else if (references_[static_cast<std::size_t>(index)] !=
                       owned.reference) {
                // Multiple persisted parents for one runtime OCCT shape are
                // deliberately ambiguous and therefore not selectable.
                ambiguous_[static_cast<std::size_t>(index)] = true;
            }
        }
    }

    [[nodiscard]] Reference reference_for(const TopoDS_Shape& shape) const {
        const int index = shapes_.FindIndex(shape);
        if (index == 0 || ambiguous_[static_cast<std::size_t>(index)]) return {};
        return references_[static_cast<std::size_t>(index)];
    }

private:
    TopTools_IndexedMapOfShape shapes_;
    std::vector<Reference> references_;
    std::vector<bool> ambiguous_;
};

template <typename Algorithm>
std::vector<TopoDS_Shape> propagate_display_edges(
    Algorithm& algorithm, const std::vector<TopoDS_Shape>& existing) {
    std::vector<TopoDS_Shape> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape) {
        if (shape.ShapeType() != TopAbs_EDGE ||
            std::any_of(propagated.begin(), propagated.end(),
                [&](const auto& value) { return value.IsSame(shape); })) {
            return;
        }
        propagated.push_back(shape);
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &algorithm.Modified(source), &algorithm.Generated(source)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != TopAbs_EDGE) continue;
                append_unique(iterator.Value());
                has_descendant = true;
            }
        }
        if (!has_descendant && !algorithm.IsDeleted(source)) {
            append_unique(source);
        }
    }
    return propagated;
}

std::vector<TopoDS_Shape> propagate_display_edges(
    const Handle(BRepTools_History)& history,
    const std::vector<TopoDS_Shape>& existing) {
    if (history.IsNull()) return existing;
    std::vector<TopoDS_Shape> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape) {
        if (shape.ShapeType() != TopAbs_EDGE ||
            std::any_of(propagated.begin(), propagated.end(),
                [&](const auto& value) { return value.IsSame(shape); })) {
            return;
        }
        propagated.push_back(shape);
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &history->Modified(source), &history->Generated(source)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != TopAbs_EDGE) continue;
                append_unique(iterator.Value());
                has_descendant = true;
            }
        }
        if (!has_descendant && !history->IsRemoved(source)) append_unique(source);
    }
    return propagated;
}

std::vector<TopoDS_Shape> cross_reference_face_edges(
    const TopoDS_Shape& shape, const std::vector<OwnedFace>& owned_faces) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> references(owned_faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    std::vector<TopoDS_Shape> result;
    for (int index = 1; index <= edge_faces.Extent(); ++index) {
        const auto& adjacent = edge_faces.FindFromIndex(index);
        if (adjacent.Extent() != 2) continue;
        TopTools_ListIteratorOfListOfShape iterator(adjacent);
        const auto first = references.reference_for(iterator.Value());
        iterator.Next();
        const auto second = references.reference_for(iterator.Value());
        if (!first.valid() || !second.valid() || first == second) continue;
        result.push_back(edge_faces.FindKey(index));
    }
    return result;
}

bool history_edge_survives(const Handle(BRepTools_History)& history,
    const TopoDS_Shape& edge, const TopTools_IndexedMapOfShape& result_edges) {
    if (result_edges.Contains(edge)) return true;
    if (history.IsNull()) return false;
    for (const auto* list : {
            &history->Modified(edge), &history->Generated(edge)}) {
        for (TopTools_ListIteratorOfListOfShape iterator(*list);
             iterator.More(); iterator.Next()) {
            if (iterator.Value().ShapeType() == TopAbs_EDGE &&
                result_edges.Contains(iterator.Value())) return true;
        }
    }
    return false;
}

struct ProvenanceUnifyResult {
    TopoDS_Shape shape;
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    std::vector<TopoDS_Shape> hidden_display_edges;
};

ProvenanceUnifyResult unify_preserving_face_provenance(
    const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& edges,
    const std::vector<OwnedVertex>& vertices,
    double tolerance) {
    ShapeUpgrade_UnifySameDomain probe(shape, true, true, false);
    probe.SetLinearTolerance(tolerance);
    probe.Build();
    if (probe.Shape().IsNull() ||
        !BRepCheck_Analyzer(probe.Shape()).IsValid()) {
        return {shape, faces, edges, vertices, {}};
    }

    TopTools_IndexedMapOfShape probe_edges;
    TopExp::MapShapes(probe.Shape(), TopAbs_EDGE, probe_edges);
    std::vector<TopoDS_Shape> protected_edges;
    for (const auto& edge : cross_reference_face_edges(shape, faces)) {
        if (!history_edge_survives(probe.History(), edge, probe_edges)) {
            protected_edges.push_back(edge);
        }
    }

    if (protected_edges.empty()) {
        return {probe.Shape(),
            propagate_topology(probe.History(), faces),
            propagate_topology(probe.History(), edges),
            propagate_topology(probe.History(), vertices), {}};
    }

    ShapeUpgrade_UnifySameDomain protected_unifier(shape, true, true, false);
    protected_unifier.SetLinearTolerance(tolerance);
    for (const auto& edge : protected_edges) {
        protected_unifier.KeepShape(edge);
    }
    protected_unifier.Build();
    if (protected_unifier.Shape().IsNull() ||
        !BRepCheck_Analyzer(protected_unifier.Shape()).IsValid()) {
        return {shape, faces, edges, vertices, protected_edges};
    }
    const auto history = protected_unifier.History();
    return {protected_unifier.Shape(),
        propagate_topology(history, faces),
        propagate_topology(history, edges),
        propagate_topology(history, vertices),
        propagate_display_edges(history, protected_edges)};
}

template <typename Algorithm>
std::vector<OwnedFace> generated_edge_treatment_faces(
    Algorithm& algorithm,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    TopTools_IndexedMapOfShape generated_shapes;
    std::vector<std::set<std::string>> parents(1);
    for (const auto& [edge, reference] : selected) {
        const std::string parent = std::to_string(reference.owner_id.size()) +
            ":" + reference.owner_id + std::to_string(reference.semantic_key.size()) +
            ":" + reference.semantic_key;
        const auto& generated = algorithm.Generated(edge);
        for (TopTools_ListIteratorOfListOfShape iterator(generated);
             iterator.More(); iterator.Next()) {
            if (iterator.Value().ShapeType() != TopAbs_FACE) continue;
            int index = generated_shapes.FindIndex(iterator.Value());
            if (index == 0) {
                index = generated_shapes.Add(iterator.Value());
                parents.resize(static_cast<std::size_t>(index) + 1);
            }
            parents[static_cast<std::size_t>(index)].insert(parent);
        }
    }
    std::vector<OwnedFace> result;
    result.reserve(static_cast<std::size_t>(generated_shapes.Extent()));
    for (int index = 1; index <= generated_shapes.Extent(); ++index) {
        std::string key(role);
        key += ":from:";
        bool first = true;
        for (const auto& parent : parents[static_cast<std::size_t>(index)]) {
            if (!first) key += "|";
            key += parent;
            first = false;
        }
        result.push_back({generated_shapes.FindKey(index),
            FaceReference{treatment_owner, std::move(key), {}}});
    }
    return result;
}

void append_unmapped_edge_treatment_faces(
    const TopoDS_Shape& result_shape,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::vector<OwnedFace>& input_faces,
    const std::string& treatment_owner,
    std::string_view role,
    double tolerance,
    std::vector<OwnedFace>& faces) {
    std::set<std::string> parents;
    for (const auto& [edge, reference] : selected) {
        static_cast<void>(edge);
        parents.insert(std::to_string(reference.owner_id.size()) + ":" +
            reference.owner_id + std::to_string(reference.semantic_key.size()) +
            ":" + reference.semantic_key);
    }
    std::string key(role);
    key += ":from:";
    bool first = true;
    for (const auto& parent : parents) {
        if (!first) key += "|";
        key += parent;
        first = false;
    }
    const FaceReference fallback_reference{
        treatment_owner, std::move(key), {}};
    TopTools_IndexedMapOfShape mapped_faces;
    for (const auto& face : faces) mapped_faces.Add(face.shape);
    const double geometric_tolerance = std::max(1.0e-6, tolerance * 10.0);
    const auto inherited_input_reference = [&](const TopoDS_Face& fragment)
            -> std::optional<FaceReference> {
        double u_min{}, u_max{}, v_min{}, v_max{};
        BRepTools::UVBounds(fragment, u_min, u_max, v_min, v_max);
        if (!std::isfinite(u_min) || !std::isfinite(u_max) ||
            !std::isfinite(v_min) || !std::isfinite(v_max)) return std::nullopt;
        TopLoc_Location fragment_location;
        const auto& fragment_surface =
            BRep_Tool::Surface(fragment, fragment_location);
        if (fragment_surface.IsNull()) return std::nullopt;

        // UV midpoints are not necessarily inside a concave trimmed face.
        // Sample a small, deterministic grid and retain only points classified
        // on the actual fragment. This is part of the explicit body calculation,
        // never a viewer/picking path.
        constexpr std::array<double, 5> fractions{
            0.5, 0.25, 0.75, 0.125, 0.875};
        std::vector<gp_Pnt> samples;
        samples.reserve(9);
        for (const double u_fraction : fractions) {
            for (const double v_fraction : fractions) {
                const double u = u_min + (u_max - u_min) * u_fraction;
                const double v = v_min + (v_max - v_min) * v_fraction;
                BRepClass_FaceClassifier classifier(
                    fragment, gp_Pnt2d(u, v), geometric_tolerance);
                if (classifier.State() != TopAbs_IN &&
                    classifier.State() != TopAbs_ON) continue;
                auto point = fragment_surface->Value(u, v);
                point.Transform(fragment_location.Transformation());
                samples.push_back(point);
                if (samples.size() == 9) break;
            }
            if (samples.size() == 9) break;
        }
        if (samples.empty()) return std::nullopt;

        int best_inside_count{};
        std::vector<FaceReference> best_references;
        for (const auto& input : input_faces) {
            if (!input.reference.valid() ||
                input.shape.ShapeType() != TopAbs_FACE) continue;
            const auto input_face = TopoDS::Face(input.shape);
            TopLoc_Location input_location;
            const auto& input_surface =
                BRep_Tool::Surface(input_face, input_location);
            if (input_surface.IsNull()) continue;
            const auto inverse_location =
                input_location.Transformation().Inverted();
            bool same_support = true;
            int inside_count{};
            for (auto point : samples) {
                point.Transform(inverse_location);
                GeomAPI_ProjectPointOnSurf projection(
                    point, input_surface, geometric_tolerance);
                if (!projection.IsDone() || projection.NbPoints() == 0 ||
                    projection.LowerDistance() > geometric_tolerance) {
                    same_support = false;
                    break;
                }
                double u{}, v{};
                projection.LowerDistanceParameters(u, v);
                BRepClass_FaceClassifier classifier(
                    input_face, gp_Pnt2d(u, v), geometric_tolerance);
                if (classifier.State() == TopAbs_IN ||
                    classifier.State() == TopAbs_ON) ++inside_count;
            }
            if (!same_support || inside_count == 0 ||
                inside_count < best_inside_count) continue;
            if (inside_count > best_inside_count) {
                best_inside_count = inside_count;
                best_references.clear();
            }
            if (std::find(best_references.begin(), best_references.end(),
                    input.reference) == best_references.end()) {
                best_references.push_back(input.reference);
            }
        }
        // Never invent an owner when two different persisted source faces are
        // geometrically indistinguishable. A unique support/trim match inherits
        // its already-defined ZIMA identity; only truly new or ambiguous faces
        // fall through to the treatment identity below.
        return best_references.size() == 1
            ? std::optional<FaceReference>{best_references.front()}
            : std::nullopt;
    };
    for (TopExp_Explorer explorer(result_shape, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        if (mapped_faces.Contains(explorer.Current())) continue;
        mapped_faces.Add(explorer.Current());
        if (const auto inherited = inherited_input_reference(
                TopoDS::Face(explorer.Current()))) {
            faces.push_back({explorer.Current(), *inherited});
            continue;
        }
        // The identity is defined exclusively by the treatment feature,
        // semantic role and persisted selected parents. OCCT traversal only
        // locates every runtime fragment belonging to that already-defined
        // identity; its enumeration position never enters the key.
        faces.push_back({explorer.Current(), fallback_reference});
    }
}

template <typename Reference>
std::string encoded_topology_reference(const Reference& reference) {
    return std::to_string(reference.owner_id.size()) + ":" +
        reference.owner_id + std::to_string(reference.semantic_key.size()) +
        ":" + reference.semantic_key +
        std::to_string(reference.instance_path.size()) + ":" +
        reference.instance_path;
}

std::string encoded_topology_reference_set(
    const std::set<std::string>& references) {
    std::string encoded;
    for (const auto& reference : references) {
        encoded += std::to_string(reference.size()) + ":" + reference;
    }
    return encoded;
}

template <typename Reference, typename Owned>
std::set<std::string> referenced_ancestor_tokens(
    const TopTools_IndexedDataMapOfShapeListOfShape& ancestors,
    const TopoDS_Shape& child,
    const TopologyReferenceIndex<Reference, Owned>& references) {
    std::set<std::string> result;
    const int child_index = ancestors.FindIndex(child);
    if (child_index == 0) return result;
    for (TopTools_ListIteratorOfListOfShape iterator(
            ancestors.FindFromIndex(child_index));
         iterator.More(); iterator.Next()) {
        const auto reference = references.reference_for(iterator.Value());
        if (reference.valid()) {
            result.insert(encoded_topology_reference(reference));
        }
    }
    return result;
}

std::set<std::string> selected_edge_parent_tokens(
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected) {
    std::set<std::string> result;
    for (const auto& [edge, reference] : selected) {
        static_cast<void>(edge);
        result.insert(encoded_topology_reference(reference));
    }
    return result;
}

std::vector<OwnedEdge> complete_edge_treatment_edges(
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& propagated_edges,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> face_references(faces);
    const TopologyReferenceIndex<EdgeReference, OwnedEdge> edge_references(
        propagated_edges);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_VERTEX, TopAbs_FACE, vertex_faces);
    const auto selected_parents = selected_edge_parent_tokens(selected);

    std::vector<OwnedEdge> result;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_EDGE);
         explorer.More(); explorer.Next()) {
        const auto edge = explorer.Current();
        if (visited.Contains(edge)) continue;
        visited.Add(edge);
        if (const auto inherited = edge_references.reference_for(edge);
            inherited.valid()) {
            result.push_back({edge, inherited});
            continue;
        }

        const auto adjacent_faces = referenced_ancestor_tokens(
            edge_faces, edge, face_references);
        std::set<std::string> endpoint_supports;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge, TopAbs_VERTEX, vertices);
        for (int index = 1; index <= vertices.Extent(); ++index) {
            const auto support = referenced_ancestor_tokens(
                vertex_faces, vertices.FindKey(index), face_references);
            if (!support.empty()) {
                endpoint_supports.insert(
                    encoded_topology_reference_set(support));
            }
        }
        std::string semantic_key(role);
        semantic_key += ":from:" +
            encoded_topology_reference_set(selected_parents);
        semantic_key += ":between:" +
            encoded_topology_reference_set(adjacent_faces);
        semantic_key += ":ends:" +
            encoded_topology_reference_set(endpoint_supports);
        result.push_back({edge,
            EdgeReference{treatment_owner, std::move(semantic_key), {}}});
    }
    return result;
}

std::vector<OwnedVertex> complete_edge_treatment_vertices(
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedEdge>& edges,
    const std::vector<OwnedVertex>& propagated_vertices,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    const TopologyReferenceIndex<EdgeReference, OwnedEdge> edge_references(edges);
    const TopologyReferenceIndex<VertexReference, OwnedVertex> vertex_references(
        propagated_vertices);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_edges;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_VERTEX, TopAbs_EDGE, vertex_edges);
    const auto selected_parents = selected_edge_parent_tokens(selected);

    std::vector<OwnedVertex> result;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_VERTEX);
         explorer.More(); explorer.Next()) {
        const auto vertex = explorer.Current();
        if (visited.Contains(vertex)) continue;
        visited.Add(vertex);
        if (const auto inherited = vertex_references.reference_for(vertex);
            inherited.valid()) {
            result.push_back({vertex, inherited});
            continue;
        }
        const auto incident_edges = referenced_ancestor_tokens(
            vertex_edges, vertex, edge_references);
        std::string semantic_key(role);
        semantic_key += ":from:" +
            encoded_topology_reference_set(selected_parents);
        semantic_key += ":at:" +
            encoded_topology_reference_set(incident_edges);
        result.push_back({vertex,
            VertexReference{treatment_owner, std::move(semantic_key), {}}});
    }
    return result;
}

std::string serialize_kernel_shape(const TopoDS_Shape& shape) {
    std::ostringstream serialized_shape;
    // Viewer triangles are persisted separately in ViewerMesh. The kernel
    // snapshot exists only to resume an explicitly requested solid
    // calculation, so storing OCCT's duplicate triangulation wastes both
    // serialization time and document space.
    BRepTools::Write(shape, serialized_shape, false, false,
        TopTools_FormatVersion_CURRENT);
    return serialized_shape.str();
}

BodyResult make_result(
    const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& owned_faces,
    const std::vector<OwnedEdge>& owned_edges,
    const std::vector<OwnedVertex>& owned_vertices,
    bool original_reference_geometry = false,
    bool persist_kernel_shape = true,
    bool collect_original_references = false,
    const std::vector<TopoDS_Shape>& hidden_display_edges = {}) {
    Bnd_Box mesh_bounds;
    BRepBndLib::Add(shape, mesh_bounds);
    Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
    double linear_deflection = 0.1;
    if (!mesh_bounds.IsVoid()) {
        mesh_bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double diagonal = std::hypot(
            std::hypot(xmax - xmin, ymax - ymin), zmax - zmin);
        // 0.1 mm is appropriate for ordinary parts but pathological for a
        // machine/plant-sized STEP compound. Keep roughly 1e-4 of the scene
        // diagonal while retaining the existing detail floor for small work.
        linear_deflection = std::max(0.1, diagonal * 1.0e-4);
    }
    BRepMesh_IncrementalMesh(
        shape, linear_deflection, false, 0.5, true).Perform();
    BodyResult result;
    GProp_GProps volume_properties;
    GProp_GProps surface_properties;
    BRepGProp::VolumeProperties(shape, volume_properties);
    BRepGProp::SurfaceProperties(shape, surface_properties);
    result.volume = volume_properties.Mass();
    result.surface_area = surface_properties.Mass();
    if (persist_kernel_shape) result.kernel_shape = serialize_kernel_shape(shape);
    std::optional<TopologyReferenceIndex<FaceReference, OwnedFace>> face_references;
    std::optional<TopologyReferenceIndex<EdgeReference, OwnedEdge>> edge_references;
    std::optional<TopologyReferenceIndex<VertexReference, OwnedVertex>>
        vertex_references;
    if (original_reference_geometry || collect_original_references) {
        face_references.emplace(owned_faces);
        vertex_references.emplace(owned_vertices);
    }
    // Display body edges remain non-reference geometry, but retain their
    // owning history container solely for cheap wire recolouring.
    edge_references.emplace(owned_edges);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    if (face_references) {
        TopExp::MapShapesAndAncestors(
            shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    }

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation =
            BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        const FaceReference reference =
            (original_reference_geometry || collect_original_references)
            ? face_references->reference_for(face)
            : FaceReference{};
        const std::uint32_t base =
            static_cast<std::uint32_t>(result.mesh.vertices.size());
        const gp_Trsf transform = location.Transformation();
        const bool collect_face =
            collect_original_references && reference.valid();
        const std::uint32_t reference_base = static_cast<std::uint32_t>(
            result.mesh.original_references.vertices.size());
        for (int node = 1; node <= triangulation->NbNodes(); ++node) {
            const gp_Pnt point = triangulation->Node(node).Transformed(transform);
            const Vec3 viewer_point{point.X(), point.Y(), point.Z()};
            result.mesh.vertices.push_back(viewer_point);
            if (collect_face) {
                result.mesh.original_references.vertices.push_back(viewer_point);
            }
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
            result.mesh.triangle_references.push_back(
                original_reference_geometry ? reference : FaceReference{});
            if (collect_face) {
                result.mesh.original_references.triangles.insert(
                    result.mesh.original_references.triangles.end(), {
                        reference_base + static_cast<std::uint32_t>(first - 1),
                        reference_base + static_cast<std::uint32_t>(second - 1),
                        reference_base + static_cast<std::uint32_t>(third - 1)});
                result.mesh.original_references.triangle_references.push_back(
                    reference);
            }
        }
    }
    TopTools_IndexedMapOfShape sampled_edges;
    TopTools_IndexedMapOfShape hidden_edges;
    for (const auto& edge : hidden_display_edges) hidden_edges.Add(edge);
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        if (sampled_edges.Contains(edge) || hidden_edges.Contains(edge)) continue;
        sampled_edges.Add(edge);
        const EdgeReference reference = edge_references->reference_for(edge);
        BRepAdaptor_Curve curve(edge);
        const int sample_count = curve.GetType() == GeomAbs_Line ? 2 : 33;
        GCPnts_UniformAbscissa samples(curve, sample_count);
        if (!samples.IsDone() || samples.NbPoints() < 2) continue;
        ViewerEdge viewer_edge;
        viewer_edge.reference = original_reference_geometry
            ? reference : EdgeReference{};
        viewer_edge.display_owner_id = reference.owner_id;
        if (face_references) {
            std::set<std::string> treatment_owners;
            const int edge_index = edge_faces.FindIndex(edge);
            if (edge_index != 0) {
                for (TopTools_ListIteratorOfListOfShape iterator(
                         edge_faces.FindFromIndex(edge_index));
                     iterator.More(); iterator.Next()) {
                    const auto face_reference =
                        face_references->reference_for(iterator.Value());
                    if (face_reference.valid() &&
                        (face_reference.semantic_key.starts_with(
                             "fillet:face") ||
                         face_reference.semantic_key.starts_with(
                             "chamfer:face"))) {
                        treatment_owners.insert(face_reference.owner_id);
                    }
                }
            }
            viewer_edge.edge_treatment_owner_ids.assign(
                treatment_owners.begin(), treatment_owners.end());
        }
        viewer_edge.points.reserve(static_cast<std::size_t>(samples.NbPoints()));
        for (int index = 1; index <= samples.NbPoints(); ++index) {
            const gp_Pnt point = curve.Value(samples.Parameter(index));
            viewer_edge.points.push_back({point.X(), point.Y(), point.Z()});
        }
        if (collect_original_references && reference.valid()) {
            auto reference_edge = viewer_edge;
            reference_edge.reference = reference;
            result.mesh.original_references.edges.push_back(
                std::move(reference_edge));
        }
        result.mesh.edges.push_back(std::move(viewer_edge));
    }
    TopTools_IndexedMapOfShape sampled_vertices;
    if (original_reference_geometry || collect_original_references) for (
        TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
        if (sampled_vertices.Contains(vertex)) continue;
        sampled_vertices.Add(vertex);
        const VertexReference reference =
            vertex_references->reference_for(vertex);
        if (!reference.valid()) continue;
        const gp_Pnt point = BRep_Tool::Pnt(vertex);
        ViewerPoint viewer_point{
            {point.X(), point.Y(), point.Z()}, reference};
        // Result-body vertices are reference targets, not permanent screen
        // markers. The viewer reveals them only while a Vertex-taking
        // command is active (or when explicitly highlighted).
        viewer_point.always_visible = false;
        if (original_reference_geometry) result.mesh.points.push_back(viewer_point);
        if (collect_original_references) {
            result.mesh.original_references.points.push_back(viewer_point);
        }
    }
    return result;
}

void append_reference_geometry(
    ViewerReferenceGeometry& target, ViewerReferenceGeometry source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        std::make_move_iterator(source.vertices.begin()),
        std::make_move_iterator(source.vertices.end()));
    for (const auto index : source.triangles) {
        target.triangles.push_back(offset + index);
    }
    target.triangle_references.insert(target.triangle_references.end(),
        std::make_move_iterator(source.triangle_references.begin()),
        std::make_move_iterator(source.triangle_references.end()));
    target.edges.insert(target.edges.end(),
        std::make_move_iterator(source.edges.begin()),
        std::make_move_iterator(source.edges.end()));
    target.points.insert(target.points.end(),
        std::make_move_iterator(source.points.begin()),
        std::make_move_iterator(source.points.end()));
    target.axes.insert(target.axes.end(),
        std::make_move_iterator(source.axes.begin()),
        std::make_move_iterator(source.axes.end()));
}

void append_original_reference_geometry(
    ViewerReferenceGeometry& target, ViewerMesh source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        source.vertices.begin(), source.vertices.end());
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!reference.valid()) continue;
        target.triangles.insert(target.triangles.end(), {
            offset + source.triangles[triangle * 3],
            offset + source.triangles[triangle * 3 + 1],
            offset + source.triangles[triangle * 3 + 2]});
        target.triangle_references.push_back(reference);
    }
    for (auto& edge : source.edges) {
        if (edge.reference.valid()) target.edges.push_back(std::move(edge));
    }
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

ViewerReferenceGeometry reference_geometry_for_owners(
    const ViewerReferenceGeometry& source,
    const std::unordered_set<std::string>& owners) {
    ViewerReferenceGeometry result;
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!owners.contains(reference.owner_id)) continue;
        const auto output_offset =
            static_cast<std::uint32_t>(result.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            result.vertices.push_back(source.vertices.at(
                source.triangles.at(triangle * 3 + corner)));
            result.triangles.push_back(output_offset + corner);
        }
        result.triangle_references.push_back(reference);
    }
    std::ranges::copy_if(source.edges, std::back_inserter(result.edges),
        [&](const auto& edge) { return owners.contains(edge.reference.owner_id); });
    std::ranges::copy_if(source.points, std::back_inserter(result.points),
        [&](const auto& point) { return owners.contains(point.reference.owner_id); });
    std::ranges::copy_if(source.axes, std::back_inserter(result.axes),
        [&](const auto& axis) { return owners.contains(axis.reference.owner_id); });
    return result;
}

void compact_history_reference_geometry(std::vector<BodyResult>& boundaries) {
    if (boundaries.size() < 2) return;
    for (auto iterator = boundaries.begin(); iterator != boundaries.end() - 1;
         ++iterator) {
        iterator->mesh.original_references = {};
    }
}

}  // namespace

struct OcctKernel::LiveCache {
    struct Topology {
        std::vector<OwnedFace> faces;
        std::vector<OwnedEdge> edges;
        std::vector<OwnedVertex> vertices;
        // Same-domain seams kept only to retain distinct persisted face
        // ancestry. They are topology, not visible model edges.
        std::vector<TopoDS_Shape> hidden_display_edges;
    };

    struct Boundary {
        TopoDS_Shape shape;
        std::shared_ptr<const Topology> topology;
    };

    std::unordered_map<std::string, Boundary> boundaries;
    std::deque<std::string> insertion_order;
    std::unordered_map<std::string, ViewerMesh> reference_meshes;
    std::deque<std::string> reference_insertion_order;
};

OcctKernel::OcctKernel() : live_cache_(std::make_unique<LiveCache>()) {}
OcctKernel::~OcctKernel() = default;

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
    std::unordered_map<std::string, StepDocumentCache> documents;
    std::vector<BodyResult> results;
    results.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const std::string owner = requests[index].reference_owner_id.empty()
            ? "step-import:" + std::to_string(index)
            : requests[index].reference_owner_id;
        const auto data = make_step_data(requests[index], owner, documents);
        auto result = make_result(data.shape, data.faces, data.edges, data.vertices,
            false, true,
            !data.faces.empty() || !data.edges.empty() || !data.vertices.empty());
        result.imported_step_topology = data.imported_step_topology;
        if (!requests[index].live_cache_fingerprint.empty()) {
            result.source_fingerprint = requests[index].live_cache_fingerprint;
            live_cache_->boundaries.insert_or_assign(
                requests[index].live_cache_fingerprint,
                LiveCache::Boundary{data.shape,
                    std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                        data.faces, data.edges, data.vertices, {}})});
            live_cache_->insertion_order.push_back(
                requests[index].live_cache_fingerprint);
            while (live_cache_->insertion_order.size() > kLiveShapeCacheLimit) {
                live_cache_->boundaries.erase(
                    live_cache_->insertion_order.front());
                live_cache_->insertion_order.pop_front();
            }
        }
        results.push_back(std::move(result));
    }
    return results;
}

BodyResult OcctKernel::compound_bodies(
    const std::vector<PlacedBody>& bodies) const {
    const auto shape = placed_compound(bodies);
    auto result = make_result(shape, {}, {}, {});
    result.source_fingerprint = "compound";
    for (const auto& body : bodies) {
        result.source_fingerprint += ":" + body.body.source_fingerprint;
    }
    return result;
}

BodyResult OcctKernel::subtract_bodies(
    const BodyResult& target,
    const BodyResult& cutter,
    Vec3 target_translation,
    Vec3 target_rotation_degrees) const {
    if (target.kernel_shape.empty() || cutter.kernel_shape.empty()) {
        throw std::invalid_argument(
            "Assembly cut requires calculated solid snapshots");
    }
    BRep_Builder builder;
    TopoDS_Shape target_shape;
    TopoDS_Shape cutter_shape;
    std::istringstream target_stream(target.kernel_shape);
    std::istringstream cutter_stream(cutter.kernel_shape);
    BRepTools::Read(target_shape, target_stream, builder);
    BRepTools::Read(cutter_shape, cutter_stream, builder);
    if (target_shape.IsNull() || cutter_shape.IsNull()) {
        throw std::runtime_error("Calculated solid snapshot is invalid");
    }
    const gp_Trsf placement = primitive_transform(
        target_translation, target_rotation_degrees);
    BRepBuilderAPI_Transform placed_target(target_shape, placement, true);
    placed_target.Build();
    if (!placed_target.IsDone()) {
        throw std::runtime_error("Assembly cut target placement failed");
    }
    BRepAlgoAPI_Cut cut(placed_target.Shape(), cutter_shape);
    cut.Build();
    if (!cut.IsDone() || cut.Shape().IsNull() ||
        !BRepCheck_Analyzer(cut.Shape()).IsValid()) {
        throw std::runtime_error("Assembly cut failed or produced an invalid solid");
    }
    BRepBuilderAPI_Transform local_result(cut.Shape(), placement.Inverted(), true);
    local_result.Build();
    if (!local_result.IsDone() || local_result.Shape().IsNull()) {
        throw std::runtime_error("Assembly cut result placement failed");
    }
    auto result = make_result(local_result.Shape(), {}, {}, {});
    // Stable references remain owned by the original component objects. The
    // boolean result topology itself is deliberately not a reference owner.
    result.mesh.original_references = target.mesh.original_references;
    result.source_fingerprint = target.source_fingerprint + ":cut:" +
        cutter.source_fingerprint;
    return result;
}

void OcctKernel::export_step(
    const std::vector<PlacedBody>& bodies, const std::string& path) const {
    STEPControl_Writer writer;
    if (writer.Transfer(placed_compound(bodies), STEPControl_AsIs) !=
            IFSelect_RetDone ||
        writer.Write(path.c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("STEP export failed");
    }
}

void OcctKernel::export_stl(
    const std::vector<PlacedBody>& bodies, const std::string& path) const {
    auto shape = placed_compound(bodies);
    BRepMesh_IncrementalMesh mesher(shape, 0.1, false, 0.5, true);
    mesher.Perform();
    if (!mesher.IsDone()) {
        throw std::runtime_error("STL triangulation failed");
    }
    StlAPI_Writer writer;
    writer.ASCIIMode() = false;
    if (!writer.Write(shape, path.c_str())) {
        throw std::runtime_error("STL export failed");
    }
}

std::vector<BodyResult> OcctKernel::evaluate_history(
    const std::vector<HistoryOperation>& operations) const {
    return evaluate_history_incremental(operations, {});
}

std::vector<BodyResult> OcctKernel::evaluate_history_incremental(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries) const {
    if (operations.empty()) return {};
    const auto first_active = std::find_if(operations.begin(), operations.end(),
        [](const auto& operation) { return !operation.suppressed; });
    if (first_active != operations.end() &&
        first_active->operation == BooleanOperation::Subtract) {
        throw std::invalid_argument("The first history operation cannot subtract");
    }
    try {
        TopoDS_Shape result_shape;
        std::shared_ptr<const LiveCache::Topology> owned_topology =
            std::make_shared<LiveCache::Topology>();
        ViewerReferenceGeometry original_references;
        std::unordered_map<std::string, StepDocumentCache> step_documents;
        std::vector<BodyResult> boundaries;
        boundaries.reserve(operations.size());
        const auto remember_live_boundary = [this](
                const std::string& fingerprint, const TopoDS_Shape& shape,
                std::shared_ptr<const LiveCache::Topology> topology) {
            if (fingerprint.empty() || shape.IsNull()) return;
            auto [iterator, inserted] =
                live_cache_->boundaries.insert_or_assign(fingerprint,
                    LiveCache::Boundary{shape, std::move(topology)});
            static_cast<void>(iterator);
            if (!inserted) return;
            live_cache_->insertion_order.push_back(fingerprint);
            while (live_cache_->insertion_order.size() > kLiveShapeCacheLimit) {
                live_cache_->boundaries.erase(
                    live_cache_->insertion_order.front());
                live_cache_->insertion_order.pop_front();
            }
        };
        std::size_t matching_prefix = 0;
        const auto available = std::min(
            operations.size(), previous_boundaries.size());
        while (matching_prefix < available &&
               previous_boundaries[matching_prefix].source_fingerprint ==
                   history_fingerprint(operations, matching_prefix + 1)) {
            ++matching_prefix;
        }
        if (matching_prefix == operations.size()) {
            std::vector<BodyResult> reused{
                previous_boundaries.begin(),
                    previous_boundaries.begin() +
                        static_cast<std::ptrdiff_t>(matching_prefix)};
            compact_history_reference_geometry(reused);
            return reused;
        }
        std::size_t reusable_prefix = matching_prefix;
        while (reusable_prefix > 0) {
            const auto& boundary = previous_boundaries[reusable_prefix - 1];
            if (!boundary.kernel_shape.empty() ||
                live_cache_->boundaries.contains(boundary.source_fingerprint)) {
                break;
            }
            --reusable_prefix;
        }
        const bool reusable_prefix_has_live_ancestry = reusable_prefix > 0 &&
            live_cache_->boundaries.contains(
                previous_boundaries[reusable_prefix - 1].source_fingerprint);
        // A persisted B-Rep is enough to reproduce geometry, but not to map
        // its runtime OCCT faces back to persisted ZIMA source identities.
        // Any explicitly requested calculation after a cold load therefore
        // rebuilds the history once; unchanged display/open paths still reuse
        // the saved Body without invoking OCCT at all.
        if (reusable_prefix > 0 && !reusable_prefix_has_live_ancestry) {
            reusable_prefix = 0;
        }
        if (reusable_prefix > 0) {
            const auto& prefix_boundary =
                previous_boundaries[reusable_prefix - 1];
            const auto cached = live_cache_->boundaries.find(
                prefix_boundary.source_fingerprint);
            if (cached == live_cache_->boundaries.end()) {
                result_shape = read_kernel_shape(prefix_boundary);
            } else {
                result_shape = cached->second.shape;
                owned_topology = cached->second.topology;
            }
            std::unordered_set<std::string> prefix_owners;
            for (std::size_t index = 0; index < reusable_prefix; ++index) {
                if (!operations[index].suppressed) {
                    prefix_owners.insert(operations[index].owner_id);
                }
            }
            original_references = reference_geometry_for_owners(
                previous_boundaries.back().mesh.original_references,
                prefix_owners);
            boundaries.insert(boundaries.end(), previous_boundaries.begin(),
                previous_boundaries.begin() +
                    static_cast<std::ptrdiff_t>(reusable_prefix));
        }
        for (std::size_t operation_index = reusable_prefix;
             operation_index < operations.size(); ++operation_index) {
            const auto& operation = operations[operation_index];
            const bool persist_boundary_shape =
                operation_index + 1 == operations.size();
            if (operation.owner_id.empty()) {
                throw std::invalid_argument("History operation owner ID is required");
            }
            if (operation.suppressed) {
                auto boundary = boundaries.empty() ? BodyResult{} : boundaries.back();
                if (persist_boundary_shape && !result_shape.IsNull()) {
                    boundary.kernel_shape = serialize_kernel_shape(result_shape);
                } else if (!persist_boundary_shape) {
                    boundary.kernel_shape.clear();
                }
                boundary.source_fingerprint = history_fingerprint(
                    operations, boundaries.size() + 1);
                remember_live_boundary(boundary.source_fingerprint, result_shape,
                    owned_topology);
                boundaries.push_back(std::move(boundary));
                continue;
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
                        })) {
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
                std::vector<std::pair<TopoDS_Edge, EdgeReference>> selected;
                for (const auto& requested : treatment.edges) {
                    bool found = false;
                    for (const auto& owned : owned_topology->edges) {
                        if (owned.reference.owner_id == requested.owner_id &&
                            owned.reference.semantic_key == requested.semantic_key) {
                            selected.emplace_back(
                                TopoDS::Edge(owned.shape), owned.reference);
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
                    for (const auto& [edge, reference] : selected) {
                        static_cast<void>(reference);
                        // OCCT expands one seed to its tangent contour. The
                        // document nevertheless persists every stable ZIMA
                        // edge in that route so generated topology can retain
                        // all parents. Do not add the same OCCT contour twice.
                        if (algorithm.Contour(edge) == 0) {
                            algorithm.Add(size, edge);
                        }
                    }
                    algorithm.Build();
                    if (!algorithm.IsDone()) throw std::runtime_error("OCCT Fillet failed");
                    auto treatment_faces = propagate_topology(
                        algorithm, owned_topology->faces,
                        std::vector<OwnedFace>{});
                    auto generated_faces = generated_edge_treatment_faces(
                        algorithm, selected, operation.owner_id, "fillet:face");
                    treatment_faces.insert(treatment_faces.end(),
                        std::make_move_iterator(generated_faces.begin()),
                        std::make_move_iterator(generated_faces.end()));
                    append_unmapped_edge_treatment_faces(
                        algorithm.Shape(), selected, owned_topology->faces,
                        operation.owner_id, "fillet:face",
                        std::max(1.0e-7, operation.boolean_tolerance),
                        treatment_faces);
                    auto treatment_topology = std::make_shared<LiveCache::Topology>(
                        LiveCache::Topology{
                            std::move(treatment_faces),
                            propagate_topology(algorithm, owned_topology->edges,
                                std::vector<OwnedEdge>{}),
                            propagate_topology(algorithm, owned_topology->vertices,
                                std::vector<OwnedVertex>{}),
                            propagate_display_edges(
                                algorithm, owned_topology->hidden_display_edges)});
                    result_shape = algorithm.Shape();
                    auto unified = unify_preserving_face_provenance(result_shape,
                        treatment_topology->faces, treatment_topology->edges,
                        treatment_topology->vertices,
                        std::max(1.0e-7, operation.boolean_tolerance));
                    result_shape = std::move(unified.shape);
                    auto completed_edges = complete_edge_treatment_edges(
                        result_shape, unified.faces, unified.edges, selected,
                        operation.owner_id, "fillet:edge");
                    auto completed_vertices = complete_edge_treatment_vertices(
                        result_shape, completed_edges, unified.vertices, selected,
                        operation.owner_id, "fillet:vertex");
                    owned_topology = std::make_shared<LiveCache::Topology>(
                        LiveCache::Topology{std::move(unified.faces),
                            std::move(completed_edges),
                            std::move(completed_vertices),
                            std::move(unified.hidden_display_edges)});
                } else {
                    BRepFilletAPI_MakeChamfer algorithm(result_shape);
                    for (const auto& [edge, reference] : selected) {
                        static_cast<void>(reference);
                        if (algorithm.Contour(edge) == 0) {
                            algorithm.Add(size, edge);
                        }
                    }
                    algorithm.Build();
                    if (!algorithm.IsDone()) throw std::runtime_error("OCCT Chamfer failed");
                    auto treatment_faces = propagate_topology(
                        algorithm, owned_topology->faces,
                        std::vector<OwnedFace>{});
                    auto generated_faces = generated_edge_treatment_faces(
                        algorithm, selected, operation.owner_id, "chamfer:face");
                    treatment_faces.insert(treatment_faces.end(),
                        std::make_move_iterator(generated_faces.begin()),
                        std::make_move_iterator(generated_faces.end()));
                    append_unmapped_edge_treatment_faces(
                        algorithm.Shape(), selected, owned_topology->faces,
                        operation.owner_id, "chamfer:face",
                        std::max(1.0e-7, operation.boolean_tolerance),
                        treatment_faces);
                    auto treatment_topology = std::make_shared<LiveCache::Topology>(
                        LiveCache::Topology{
                            std::move(treatment_faces),
                            propagate_topology(algorithm, owned_topology->edges,
                                std::vector<OwnedEdge>{}),
                            propagate_topology(algorithm, owned_topology->vertices,
                                std::vector<OwnedVertex>{}),
                            propagate_display_edges(
                                algorithm, owned_topology->hidden_display_edges)});
                    result_shape = algorithm.Shape();
                    auto unified = unify_preserving_face_provenance(result_shape,
                        treatment_topology->faces, treatment_topology->edges,
                        treatment_topology->vertices,
                        std::max(1.0e-7, operation.boolean_tolerance));
                    result_shape = std::move(unified.shape);
                    auto completed_edges = complete_edge_treatment_edges(
                        result_shape, unified.faces, unified.edges, selected,
                        operation.owner_id, "chamfer:edge");
                    auto completed_vertices = complete_edge_treatment_vertices(
                        result_shape, completed_edges, unified.vertices, selected,
                        operation.owner_id, "chamfer:vertex");
                    owned_topology = std::make_shared<LiveCache::Topology>(
                        LiveCache::Topology{std::move(unified.faces),
                            std::move(completed_edges),
                            std::move(completed_vertices),
                            std::move(unified.hidden_display_edges)});
                }
                boundaries.push_back(
                    make_result(result_shape, owned_topology->faces,
                        owned_topology->edges, owned_topology->vertices,
                        true, persist_boundary_shape, false,
                        owned_topology->hidden_display_edges));
                boundaries.back().source_fingerprint =
                    history_fingerprint(operations, boundaries.size());
                remember_live_boundary(boundaries.back().source_fingerprint,
                    result_shape, owned_topology);
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
                        std::none_of(owned_topology->faces.begin(),
                            owned_topology->faces.end(),
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
                        for (const auto& face : owned_topology->faces) {
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
                    double through_all_forward_span = 2'000'000.0;
                    double through_all_reverse_span = 2'000'000.0;
                    if (primitive.extent == ExtrusionRequest::Extent::ThroughAll &&
                        !result_shape.IsNull()) {
                        const Vec3 profile_origin = std::visit([](const auto& profile) {
                            using Profile = std::decay_t<decltype(profile)>;
                            if constexpr (std::is_same_v<Profile,
                                              ExtrusionRequest::PolygonProfile>) {
                                return profile.vertices.front();
                            } else if constexpr (std::is_same_v<Profile,
                                                     ExtrusionRequest::CircleProfile> ||
                                                 std::is_same_v<Profile,
                                                     ExtrusionRequest::EllipseProfile>) {
                                return profile.center;
                            } else {
                                return std::visit([](const auto& curve) {
                                    return curve.start;
                                }, profile.curves.front());
                            }
                        }, primitive.outer_profile);
                        const double direction_length = std::hypot(std::hypot(
                            primitive.direction.x, primitive.direction.y),
                            primitive.direction.z);
                        const Vec3 unit{primitive.direction.x / direction_length,
                            primitive.direction.y / direction_length,
                            primitive.direction.z / direction_length};
                        Bnd_Box bounds;
                        BRepBndLib::Add(result_shape, bounds);
                        Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                        double maximum_forward_projection{};
                        double maximum_reverse_projection{};
                        for (unsigned mask = 0; mask < 8; ++mask) {
                            const Vec3 corner{mask & 1 ? xmax : xmin,
                                mask & 2 ? ymax : ymin, mask & 4 ? zmax : zmin};
                            const double projection =
                                (corner.x-profile_origin.x)*unit.x +
                                (corner.y-profile_origin.y)*unit.y +
                                (corner.z-profile_origin.z)*unit.z;
                            maximum_forward_projection = std::max(
                                maximum_forward_projection, projection);
                            maximum_reverse_projection = std::max(
                                maximum_reverse_projection, -projection);
                        }
                        const double diagonal = std::hypot(std::hypot(
                            xmax-xmin, ymax-ymin), zmax-zmin);
                        const double margin = std::max(1.0, diagonal * 1.0e-4);
                        through_all_forward_span =
                            std::max(0.0, maximum_forward_projection) + margin;
                        through_all_reverse_span =
                            std::max(0.0, maximum_reverse_projection) + margin;
                    }
                    return make_extrusion_data(
                        primitive, operation.owner_id, exact_target,
                        through_all_forward_span, through_all_reverse_span);
                } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                    validate_revolution(primitive);
                    return make_revolution_data(primitive, operation.owner_id);
                } else if constexpr (std::is_same_v<Request, StepRequest>) {
                    return make_step_data(primitive, operation.owner_id, step_documents);
                } else {
                    throw std::logic_error("Edge treatment reached primitive builder");
                }
            }, operation.primitive);
            const std::string reference_cache_key = history_fingerprint(
                std::vector<HistoryOperation>{operation}, 1);
            const auto* extrusion_request =
                std::get_if<ExtrusionRequest>(&operation.primitive);
            const bool cache_reference_mesh =
                !imported_step &&
                (extrusion_request == nullptr ||
                 extrusion_request->extent == ExtrusionRequest::Extent::Blind);
            const bool standalone_import = imported_step && result_shape.IsNull();
            std::optional<BodyResult> standalone_import_result;
            ViewerMesh operand_mesh;
            const auto cached_reference = cache_reference_mesh
                ? live_cache_->reference_meshes.find(reference_cache_key)
                : live_cache_->reference_meshes.end();
            if (cached_reference != live_cache_->reference_meshes.end()) {
                operand_mesh = cached_reference->second;
            } else if (imported_step) {
                auto operand_result = make_result(
                    operand.shape, operand.faces, operand.edges,
                    operand.vertices, standalone_import,
                    standalone_import && persist_boundary_shape, true);
                append_reference_geometry(original_references,
                    std::move(operand_result.mesh.original_references));
                if (standalone_import) {
                    standalone_import_result = std::move(operand_result);
                }
            } else {
                auto operand_result = make_result(
                    operand.shape, operand.faces, operand.edges,
                    operand.vertices, true, false);
                operand_result.mesh.axes = axes_for_operation(operation, operand.shape);
                operand_mesh = std::move(operand_result.mesh);
                if (cache_reference_mesh) {
                    live_cache_->reference_meshes.emplace(
                        reference_cache_key, operand_mesh);
                    live_cache_->reference_insertion_order.push_back(
                        reference_cache_key);
                    while (live_cache_->reference_insertion_order.size() >
                           kLiveShapeCacheLimit) {
                        live_cache_->reference_meshes.erase(
                            live_cache_->reference_insertion_order.front());
                        live_cache_->reference_insertion_order.pop_front();
                    }
                }
            }
            if (!imported_step) {
                append_original_reference_geometry(
                    original_references, std::move(operand_mesh));
            }
            if (result_shape.IsNull()) {
                result_shape = operand.shape;
                owned_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{
                        operand.faces, operand.edges, operand.vertices, {}});
            } else if (imported_step && operation.operation == BooleanOperation::Add) {
                TopoDS_Compound compound;
                BRep_Builder builder;
                builder.MakeCompound(compound);
                builder.Add(compound, result_shape);
                builder.Add(compound, operand.shape);
                result_shape = compound;
                auto combined_topology = std::make_shared<LiveCache::Topology>(
                    *owned_topology);
                combined_topology->faces.insert(combined_topology->faces.end(),
                    operand.faces.begin(), operand.faces.end());
                combined_topology->edges.insert(combined_topology->edges.end(),
                    operand.edges.begin(), operand.edges.end());
                combined_topology->vertices.insert(
                    combined_topology->vertices.end(), operand.vertices.begin(),
                    operand.vertices.end());
                owned_topology = std::move(combined_topology);
            } else if (operation.operation == BooleanOperation::Add) {
                BRepAlgoAPI_Fuse algorithm(result_shape, operand.shape);
                algorithm.SetToFillHistory(true);
                algorithm.SetFuzzyValue(
                    std::max(1.0e-7, operation.boolean_tolerance));
                algorithm.Build();
                if (!algorithm.IsDone() || algorithm.Shape().IsNull() ||
                    !BRepCheck_Analyzer(algorithm.Shape()).IsValid()) {
                    throw std::runtime_error("OCCT fuse failed");
                }
                auto fused_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{
                        propagate_topology(algorithm, owned_topology->faces,
                            operand.faces),
                        propagate_topology(algorithm, owned_topology->edges,
                            operand.edges),
                        propagate_topology(algorithm, owned_topology->vertices,
                            operand.vertices),
                        propagate_display_edges(
                            algorithm, owned_topology->hidden_display_edges)});
                result_shape = algorithm.Shape();
                auto unified = unify_preserving_face_provenance(result_shape,
                    fused_topology->faces, fused_topology->edges,
                    fused_topology->vertices,
                    std::max(1.0e-7, operation.boolean_tolerance));
                result_shape = std::move(unified.shape);
                owned_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{std::move(unified.faces),
                        std::move(unified.edges), std::move(unified.vertices),
                        std::move(unified.hidden_display_edges)});
            } else {
                BRepAlgoAPI_Cut algorithm(result_shape, operand.shape);
                algorithm.SetToFillHistory(true);
                algorithm.SetFuzzyValue(
                    std::max(1.0e-7, operation.boolean_tolerance));
                algorithm.Build();
                if (!algorithm.IsDone()) throw std::runtime_error("OCCT cut failed");
                owned_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{
                        propagate_topology(algorithm, owned_topology->faces,
                            operand.faces),
                        propagate_topology(algorithm, owned_topology->edges,
                            operand.edges),
                        propagate_topology(algorithm, owned_topology->vertices,
                            operand.vertices),
                        propagate_display_edges(
                            algorithm, owned_topology->hidden_display_edges)});
                result_shape = algorithm.Shape();
            }
            if (standalone_import_result) {
                boundaries.push_back(std::move(*standalone_import_result));
            } else {
                boundaries.push_back(make_result(
                    result_shape, owned_topology->faces, owned_topology->edges,
                    owned_topology->vertices,
                    true, persist_boundary_shape, false,
                    owned_topology->hidden_display_edges));
            }
            if (imported_step) {
                boundaries.back().imported_step_topology =
                    operand.imported_step_topology;
            }
            boundaries.back().source_fingerprint =
                history_fingerprint(operations, boundaries.size());
            remember_live_boundary(boundaries.back().source_fingerprint,
                result_shape, owned_topology);
        }
        if (!boundaries.empty()) {
            boundaries.back().mesh.original_references =
                std::move(original_references);
            // Feature-generated axes are both persisted references and
            // ordinary visible construction geometry. Keeping them only in
            // original_references made the kernel contract pass while the
            // user saw no axis in normal View.
            for (const auto& axis :
                 boundaries.back().mesh.original_references.axes) {
                if (axis.reference.semantic_key == "axis:primary" ||
                    axis.reference.semantic_key.starts_with("axis:profile:")) {
                    boundaries.back().mesh.axes.push_back(axis);
                }
            }
        }
        compact_history_reference_geometry(boundaries);
        return boundaries;
    } catch (const Standard_Failure& failure) {
        throw std::runtime_error(failure.GetMessageString());
    }
}

}  // namespace zima::kernel
