#include <zima/drawing/drawing_document.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace zima::drawing {
namespace {

std::string make_id() {
    std::mt19937_64 generator(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
}

const char* format_name(SheetFormat value) {
    switch (value) {
        case SheetFormat::A4: return "A4";
        case SheetFormat::A3: return "A3";
        case SheetFormat::A2: return "A2";
        case SheetFormat::A1: return "A1";
        case SheetFormat::A0: return "A0";
    }
    throw std::runtime_error("Invalid Drawing sheet format");
}

SheetFormat parse_format(const std::string& value) {
    if (value == "A4") return SheetFormat::A4;
    if (value == "A3") return SheetFormat::A3;
    if (value == "A2") return SheetFormat::A2;
    if (value == "A1") return SheetFormat::A1;
    if (value == "A0") return SheetFormat::A0;
    throw std::runtime_error("Unsupported Drawing sheet format: " + value);
}

const char* orientation_name(ViewOrientation value) {
    switch (value) {
        case ViewOrientation::Front: return "front";
        case ViewOrientation::Back: return "back";
        case ViewOrientation::Left: return "left";
        case ViewOrientation::Right: return "right";
        case ViewOrientation::Top: return "top";
        case ViewOrientation::Bottom: return "bottom";
        case ViewOrientation::Isometric: return "isometric";
    }
    throw std::runtime_error("Invalid Drawing view orientation");
}

ViewOrientation parse_orientation(const std::string& value) {
    if (value == "front") return ViewOrientation::Front;
    if (value == "back") return ViewOrientation::Back;
    if (value == "left") return ViewOrientation::Left;
    if (value == "right") return ViewOrientation::Right;
    if (value == "top") return ViewOrientation::Top;
    if (value == "bottom") return ViewOrientation::Bottom;
    if (value == "isometric") return ViewOrientation::Isometric;
    throw std::runtime_error("Unsupported Drawing view orientation: " + value);
}

const char* display_name(DisplayStyle value) {
    switch (value) {
        case DisplayStyle::VisibleEdges: return "visible_edges";
        case DisplayStyle::HiddenEdges: return "hidden_edges";
        case DisplayStyle::ShadedWithEdges: return "shaded_with_edges";
    }
    throw std::runtime_error("Invalid Drawing display style");
}

DisplayStyle parse_display(const std::string& value) {
    if (value == "visible_edges") return DisplayStyle::VisibleEdges;
    if (value == "hidden_edges") return DisplayStyle::HiddenEdges;
    if (value == "shaded_with_edges") return DisplayStyle::ShadedWithEdges;
    throw std::runtime_error("Unsupported Drawing display style: " + value);
}

nlohmann::json edge_reference_json(const zima::kernel::EdgeReference& reference) {
    return {{"owner", reference.owner_id}, {"key", reference.semantic_key},
            {"instance_path", reference.instance_path}};
}

zima::kernel::EdgeReference parse_edge_reference(const nlohmann::json& value) {
    return {value.value("owner", ""), value.value("key", ""),
            value.value("instance_path", "")};
}

struct ProjectedPoint { Point2 paper; double depth{}; };

ProjectedPoint projected_with_depth(
    const zima::kernel::Vec3& point, ViewOrientation orientation) {
    constexpr double inv_sqrt_two = 0.7071067811865475244;
    constexpr double inv_sqrt_six = 0.4082482904638630164;
    switch (orientation) {
        case ViewOrientation::Front: return {{-point.x, point.z}, -point.y};
        case ViewOrientation::Back: return {{point.x, point.z}, point.y};
        case ViewOrientation::Left: return {{-point.y, point.z}, point.x};
        case ViewOrientation::Right: return {{point.y, point.z}, -point.x};
        case ViewOrientation::Top: return {{-point.x, point.y}, point.z};
        case ViewOrientation::Bottom: return {{-point.x, -point.y}, -point.z};
        case ViewOrientation::Isometric:
            return {{(point.y - point.x) * inv_sqrt_two,
                     (point.x + point.y - 2.0 * point.z) * -inv_sqrt_six},
                    -(point.x + point.y + point.z) / std::sqrt(3.0)};
    }
    return {};
}

Point2 projected(const zima::kernel::Vec3& point, ViewOrientation orientation) {
    return projected_with_depth(point, orientation).paper;
}

bool point_in_triangle(Point2 point, Point2 a, Point2 b, Point2 c,
                       double* wa, double* wb, double* wc) {
    const double denominator = (b.y - c.y) * (a.x - c.x) +
                               (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) < 1e-12) return false;
    *wa = ((b.y - c.y) * (point.x - c.x) +
           (c.x - b.x) * (point.y - c.y)) / denominator;
    *wb = ((c.y - a.y) * (point.x - c.x) +
           (a.x - c.x) * (point.y - c.y)) / denominator;
    *wc = 1.0 - *wa - *wb;
    constexpr double tolerance = 1e-8;
    return *wa >= -tolerance && *wb >= -tolerance && *wc >= -tolerance;
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) throw std::runtime_error(std::string(field) + " must be finite");
}

}  // namespace

double DrawingSheet::width_mm() const {
    switch (format) {
        case SheetFormat::A4: return 210.0;
        case SheetFormat::A3: return 420.0;
        case SheetFormat::A2: return 594.0;
        case SheetFormat::A1: return 841.0;
        case SheetFormat::A0: return 1189.0;
    }
    return 210.0;
}

double DrawingSheet::height_mm() const {
    switch (format) {
        case SheetFormat::A4: return 297.0;
        case SheetFormat::A3: return 297.0;
        case SheetFormat::A2: return 420.0;
        case SheetFormat::A1: return 594.0;
        case SheetFormat::A0: return 841.0;
    }
    return 297.0;
}

std::vector<ProjectedEdge> project_edges(
    const zima::kernel::ViewerMesh& mesh, ViewOrientation orientation) {
    const auto& source = mesh.edges.empty() ? mesh.original_references.edges : mesh.edges;
    std::vector<ProjectedEdge> result;
    result.reserve(source.size() * 2);
    for (const auto& edge : source) {
        if (edge.points.size() < 2) continue;
        for (std::size_t segment = 1; segment < edge.points.size(); ++segment) {
            ProjectedEdge projected_edge;
            projected_edge.source = edge.reference;
            projected_edge.points = {projected(edge.points[segment - 1], orientation),
                                     projected(edge.points[segment], orientation)};
            const zima::kernel::Vec3 midpoint{
                (edge.points[segment - 1].x + edge.points[segment].x) * 0.5,
                (edge.points[segment - 1].y + edge.points[segment].y) * 0.5,
                (edge.points[segment - 1].z + edge.points[segment].z) * 0.5};
            const auto projected_midpoint = projected_with_depth(midpoint, orientation);
            for (std::size_t triangle = 0; triangle + 2 < mesh.triangles.size(); triangle += 3) {
                const auto ia = mesh.triangles[triangle];
                const auto ib = mesh.triangles[triangle + 1];
                const auto ic = mesh.triangles[triangle + 2];
                if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
                    ic >= mesh.vertices.size()) continue;
                const auto a = projected_with_depth(mesh.vertices[ia], orientation);
                const auto b = projected_with_depth(mesh.vertices[ib], orientation);
                const auto c = projected_with_depth(mesh.vertices[ic], orientation);
                double wa{}, wb{}, wc{};
                if (!point_in_triangle(projected_midpoint.paper, a.paper, b.paper, c.paper,
                                       &wa, &wb, &wc)) continue;
                const double triangle_depth = wa * a.depth + wb * b.depth + wc * c.depth;
                if (triangle_depth > projected_midpoint.depth + 1e-6) {
                    projected_edge.hidden = true;
                    break;
                }
            }
            result.push_back(std::move(projected_edge));
        }
    }
    return result;
}

std::vector<ProjectedTriangle> project_triangles(
    const zima::kernel::ViewerMesh& mesh, ViewOrientation orientation) {
    std::vector<ProjectedTriangle> result;
    result.reserve(mesh.triangles.size() / 3);
    const auto view_direction = [&] {
        switch (orientation) {
            case ViewOrientation::Front: return zima::kernel::Vec3{0, -1, 0};
            case ViewOrientation::Back: return zima::kernel::Vec3{0, 1, 0};
            case ViewOrientation::Left: return zima::kernel::Vec3{1, 0, 0};
            case ViewOrientation::Right: return zima::kernel::Vec3{-1, 0, 0};
            case ViewOrientation::Top: return zima::kernel::Vec3{0, 0, 1};
            case ViewOrientation::Bottom: return zima::kernel::Vec3{0, 0, -1};
            case ViewOrientation::Isometric: {
                const double value = -1.0 / std::sqrt(3.0);
                return zima::kernel::Vec3{value, value, value};
            }
        }
        return zima::kernel::Vec3{};
    }();
    for (std::size_t triangle = 0; triangle + 2 < mesh.triangles.size(); triangle += 3) {
        const auto ia = mesh.triangles[triangle]; const auto ib = mesh.triangles[triangle + 1];
        const auto ic = mesh.triangles[triangle + 2];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
        const auto& a3 = mesh.vertices[ia]; const auto& b3 = mesh.vertices[ib]; const auto& c3 = mesh.vertices[ic];
        const auto a = projected_with_depth(a3, orientation);
        const auto b = projected_with_depth(b3, orientation);
        const auto c = projected_with_depth(c3, orientation);
        const zima::kernel::Vec3 ab{b3.x-a3.x, b3.y-a3.y, b3.z-a3.z};
        const zima::kernel::Vec3 ac{c3.x-a3.x, c3.y-a3.y, c3.z-a3.z};
        const zima::kernel::Vec3 normal{ab.y*ac.z-ab.z*ac.y,
            ab.z*ac.x-ab.x*ac.z, ab.x*ac.y-ab.y*ac.x};
        const double normal_length = std::sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
        const double facing = normal_length <= 1e-12 ? 0.0 : std::abs(
            (normal.x*view_direction.x + normal.y*view_direction.y + normal.z*view_direction.z) /
            normal_length);
        ProjectedTriangle value;
        value.points = {a.paper, b.paper, c.paper};
        value.depth = (a.depth + b.depth + c.depth) / 3.0;
        value.light = 0.55 + 0.4 * facing;
        const std::size_t face_index = triangle / 3;
        if (face_index < mesh.triangle_references.size()) value.source = mesh.triangle_references[face_index];
        result.push_back(std::move(value));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.depth < right.depth;
    });
    return result;
}

DrawingDocument DrawingDocument::create_default() {
    DrawingDocument document;
    document.document_id = make_id();
    DrawingSheet sheet;
    sheet.id = make_id();
    document.sheets.push_back(std::move(sheet));
    return document;
}

DrawingView DrawingDocument::create_view(
    std::string source_document_id, std::filesystem::path source_path,
    const zima::kernel::ViewerMesh& source_mesh, ViewOrientation orientation) {
    if (source_document_id.empty()) throw std::invalid_argument("Drawing source ID is empty");
    DrawingView view;
    view.id = make_id();
    view.source_document_id = std::move(source_document_id);
    view.source_path = std::move(source_path);
    view.orientation = orientation;
    view.projected_edges = project_edges(source_mesh, orientation);
    view.projected_triangles = project_triangles(source_mesh, orientation);
    return view;
}

DrawingSheet* DrawingDocument::find_sheet(const std::string& id) {
    const auto found = std::find_if(sheets.begin(), sheets.end(),
        [&](const auto& sheet) { return sheet.id == id; });
    return found == sheets.end() ? nullptr : &*found;
}
const DrawingSheet* DrawingDocument::find_sheet(const std::string& id) const {
    return const_cast<DrawingDocument*>(this)->find_sheet(id);
}
DrawingView* DrawingDocument::find_view(const std::string& id) {
    for (auto& sheet : sheets) {
        const auto found = std::find_if(sheet.views.begin(), sheet.views.end(),
            [&](const auto& view) { return view.id == id; });
        if (found != sheet.views.end()) return &*found;
    }
    return nullptr;
}
const DrawingView* DrawingDocument::find_view(const std::string& id) const {
    return const_cast<DrawingDocument*>(this)->find_view(id);
}

void DrawingDocument::refresh_view(
    const std::string& view_id, const zima::kernel::ViewerMesh& source_mesh) {
    auto* view = find_view(view_id);
    if (view == nullptr) throw std::invalid_argument("Drawing view does not exist");
    view->projected_edges = project_edges(source_mesh, view->orientation);
    view->projected_triangles = project_triangles(source_mesh, view->orientation);
}

void DrawingDocument::save(const std::filesystem::path& path) const {
    if (document_id.empty() || name.empty() || sheets.empty()) {
        throw std::runtime_error("Drawing identity, name and sheets are required");
    }
    nlohmann::json root{{"format", "zima-cad-drawing"}, {"version", 1},
                        {"document_id", document_id}, {"name", name}};
    root["sheets"] = nlohmann::json::array();
    std::unordered_set<std::string> ids;
    for (const auto& sheet : sheets) {
        if (sheet.id.empty() || !ids.insert(sheet.id).second || sheet.name.empty())
            throw std::runtime_error("Drawing sheet IDs and names must be unique and non-empty");
        require_finite(sheet.default_scale, "sheet scale");
        if (sheet.default_scale <= 0.0) throw std::runtime_error("Drawing sheet scale must be positive");
        nlohmann::json serialized{{"id", sheet.id}, {"name", sheet.name},
            {"format", format_name(sheet.format)},
            {"projection_method", sheet.projection_method == ProjectionMethod::FirstAngle ? "first_angle" : "third_angle"},
            {"default_scale", sheet.default_scale}};
        serialized["views"] = nlohmann::json::array();
        for (const auto& view : sheet.views) {
            if (view.id.empty() || !ids.insert(view.id).second || view.source_document_id.empty())
                throw std::runtime_error("Drawing view identity and source are required");
            if (!std::isfinite(view.x) || !std::isfinite(view.y) ||
                !std::isfinite(view.scale) || view.scale <= 0.0)
                throw std::runtime_error("Invalid Drawing view placement or scale");
            if (!view.parent_view_id.empty() &&
                (view.parent_view_id == view.id ||
                 std::none_of(sheet.views.begin(), sheet.views.end(), [&](const auto& candidate) {
                     return candidate.id == view.parent_view_id;
                 })))
                throw std::runtime_error("Drawing projected view has an invalid parent");
            nlohmann::json item{{"id", view.id}, {"name", view.name},
                {"source_document_id", view.source_document_id},
                {"source_path", view.source_path.generic_string()},
                {"parent_view_id", view.parent_view_id},
                {"orientation", orientation_name(view.orientation)},
                {"display_style", display_name(view.display_style)},
                {"x", view.x}, {"y", view.y}, {"scale", view.scale}};
            item["projected_edges"] = nlohmann::json::array();
            for (const auto& edge : view.projected_edges) {
                nlohmann::json edge_json{{"source", edge_reference_json(edge.source)},
                                         {"hidden", edge.hidden}};
                edge_json["points"] = nlohmann::json::array();
                for (const auto& point : edge.points) edge_json["points"].push_back({point.x, point.y});
                item["projected_edges"].push_back(std::move(edge_json));
            }
            item["projected_triangles"] = nlohmann::json::array();
            for (const auto& triangle : view.projected_triangles) {
                nlohmann::json triangle_json{{"source", {{"owner", triangle.source.owner_id},
                    {"key", triangle.source.semantic_key}, {"instance_path", triangle.source.instance_path}}},
                    {"depth", triangle.depth}, {"light", triangle.light}};
                triangle_json["points"] = nlohmann::json::array();
                for (const auto& point : triangle.points) triangle_json["points"].push_back({point.x, point.y});
                item["projected_triangles"].push_back(std::move(triangle_json));
            }
            serialized["views"].push_back(std::move(item));
        }
        serialized["dimensions"] = nlohmann::json::array();
        for (const auto& dimension : sheet.dimensions) {
            if (dimension.id.empty() || !ids.insert(dimension.id).second ||
                find_view(dimension.view_id) == nullptr)
                throw std::runtime_error("Invalid Drawing dimension identity or view");
            serialized["dimensions"].push_back({{"id", dimension.id},
                {"view_id", dimension.view_id}, {"first", edge_reference_json(dimension.first)},
                {"second", edge_reference_json(dimension.second)},
                {"first_point", {dimension.first_point.x, dimension.first_point.y}},
                {"second_point", {dimension.second_point.x, dimension.second_point.y}},
                {"label_position", {dimension.label_position.x, dimension.label_position.y}},
                {"measured_value", dimension.measured_value}});
        }
        root["sheets"].push_back(std::move(serialized));
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("Cannot write Drawing document");
    stream << root.dump(2) << '\n';
}

DrawingDocument DrawingDocument::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read Drawing document");
    nlohmann::json root;
    stream >> root;
    if (root.value("format", "") != "zima-cad-drawing" || root.value("version", 0) != 1)
        throw std::runtime_error("Unsupported Drawing document format");
    DrawingDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    for (const auto& serialized : root.at("sheets")) {
        DrawingSheet sheet;
        sheet.id = serialized.at("id").get<std::string>();
        sheet.name = serialized.at("name").get<std::string>();
        sheet.format = parse_format(serialized.at("format").get<std::string>());
        sheet.projection_method = serialized.value("projection_method", "first_angle") == "third_angle"
            ? ProjectionMethod::ThirdAngle : ProjectionMethod::FirstAngle;
        sheet.default_scale = serialized.value("default_scale", 1.0);
        for (const auto& item : serialized.at("views")) {
            DrawingView view;
            view.id = item.at("id").get<std::string>();
            view.name = item.value("name", "Pohled");
            view.source_document_id = item.at("source_document_id").get<std::string>();
            view.source_path = item.value("source_path", "");
            view.parent_view_id = item.value("parent_view_id", "");
            view.orientation = parse_orientation(item.at("orientation").get<std::string>());
            view.display_style = parse_display(item.value("display_style", "visible_edges"));
            view.x = item.at("x").get<double>(); view.y = item.at("y").get<double>();
            view.scale = item.at("scale").get<double>();
            for (const auto& edge_json : item.at("projected_edges")) {
                ProjectedEdge edge;
                edge.source = parse_edge_reference(edge_json.at("source"));
                edge.hidden = edge_json.value("hidden", false);
                for (const auto& point : edge_json.at("points"))
                    edge.points.push_back({point.at(0).get<double>(), point.at(1).get<double>()});
                view.projected_edges.push_back(std::move(edge));
            }
            for (const auto& triangle_json : item.value("projected_triangles", nlohmann::json::array())) {
                ProjectedTriangle triangle;
                triangle.source = {triangle_json.at("source").value("owner", ""),
                    triangle_json.at("source").value("key", ""),
                    triangle_json.at("source").value("instance_path", "")};
                triangle.depth = triangle_json.value("depth", 0.0);
                triangle.light = triangle_json.value("light", 0.8);
                for (std::size_t index = 0; index < 3; ++index)
                    triangle.points[index] = {triangle_json.at("points").at(index).at(0).get<double>(),
                                              triangle_json.at("points").at(index).at(1).get<double>()};
                view.projected_triangles.push_back(std::move(triangle));
            }
            sheet.views.push_back(std::move(view));
        }
        for (const auto& item : serialized.value("dimensions", nlohmann::json::array())) {
            LinearDimension dimension;
            dimension.id = item.at("id").get<std::string>();
            dimension.view_id = item.at("view_id").get<std::string>();
            dimension.first = parse_edge_reference(item.at("first"));
            dimension.second = parse_edge_reference(item.at("second"));
            dimension.first_point = {item.at("first_point").at(0).get<double>(), item.at("first_point").at(1).get<double>()};
            dimension.second_point = {item.at("second_point").at(0).get<double>(), item.at("second_point").at(1).get<double>()};
            dimension.label_position = {item.at("label_position").at(0).get<double>(), item.at("label_position").at(1).get<double>()};
            dimension.measured_value = item.at("measured_value").get<double>();
            sheet.dimensions.push_back(std::move(dimension));
        }
        document.sheets.push_back(std::move(sheet));
    }
    if (document.sheets.empty()) throw std::runtime_error("Drawing has no sheets");
    return document;
}

}  // namespace zima::drawing
