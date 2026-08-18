#include <zima/interchange/dxf.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>

namespace zima::interchange {
namespace {

struct Pair { int code{}; std::string value; };

std::vector<Pair> read_pairs(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Nelze otevřít DXF soubor");
    std::vector<Pair> result;
    std::string code;
    std::string value;
    while (std::getline(input, code) && std::getline(input, value)) {
        const auto trim = [](std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            text = first == std::string::npos ? std::string{} :
                text.substr(first, last - first + 1);
        };
        trim(code);
        trim(value);
        try { result.push_back({std::stoi(code), value}); }
        catch (const std::exception&) { throw std::runtime_error("Neplatný DXF group code"); }
    }
    if (!input.eof()) throw std::runtime_error("Neúplný DXF pár");
    return result;
}

double number(const std::unordered_map<int, std::string>& values, int code) {
    const auto found = values.find(code);
    if (found == values.end()) throw std::runtime_error("DXF entitě chybí souřadnice");
    return std::stod(found->second);
}

void pair(std::ostream& output, int code, const auto& value) {
    output << code << '\n' << value << '\n';
}

}  // namespace

DxfImportResult import_dxf(
    const std::filesystem::path& path, zima::sketcher::Sketch& target,
    double ambiguous_unit_scale_to_mm, std::size_t maximum_entities) {
    if (!std::isfinite(ambiguous_unit_scale_to_mm) || ambiguous_unit_scale_to_mm <= 0.0) {
        throw std::invalid_argument("Měřítko DXF musí být kladné");
    }
    if (maximum_entities == 0) {
        throw std::invalid_argument("Limit DXF entit musí být kladný");
    }
    const auto pairs = read_pairs(path);
    DxfImportResult result;
    bool preflight_entities{};
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        if (pairs[index].code == 0 && pairs[index].value == "SECTION" &&
            index + 1 < pairs.size() && pairs[index + 1].code == 2 &&
            pairs[index + 1].value == "ENTITIES") {
            preflight_entities = true;
            ++index;
            continue;
        }
        if (preflight_entities && pairs[index].code == 0 &&
            pairs[index].value == "ENDSEC") {
            preflight_entities = false;
            continue;
        }
        if (preflight_entities && pairs[index].code == 0) {
            ++result.source_entities;
        }
    }
    if (result.source_entities > maximum_entities) {
        throw std::runtime_error("DXF obsahuje " +
            std::to_string(result.source_entities) +
            " entit; bezpečnostní limit je " +
            std::to_string(maximum_entities) + ". Import byl zrušen.");
    }
    std::vector<std::string> geometry_ids;
    struct CoordinateKey {
        std::uint64_t x{};
        std::uint64_t y{};
        bool operator==(const CoordinateKey&) const = default;
    };
    struct CoordinateHash {
        std::size_t operator()(const CoordinateKey& value) const {
            return std::hash<std::uint64_t>{}(value.x) ^
                (std::hash<std::uint64_t>{}(value.y) << 1U);
        }
    };
    std::unordered_map<CoordinateKey, std::string, CoordinateHash> exact_points;
    for (const auto& point : target.points) {
        exact_points.emplace(CoordinateKey{std::bit_cast<std::uint64_t>(point.x),
            std::bit_cast<std::uint64_t>(point.y)}, point.id);
    }
    const auto point_id = [&](double x, double y) {
        const CoordinateKey key{std::bit_cast<std::uint64_t>(x),
            std::bit_cast<std::uint64_t>(y)};
        if (const auto found = exact_points.find(key); found != exact_points.end()) {
            return found->second;
        }
        auto point = zima::sketcher::Sketch::create_point(x, y);
        const auto id = point.id;
        target.points.push_back(std::move(point));
        exact_points.emplace(key, id);
        return id;
    };
    bool entities{};
    for (std::size_t index = 0; index < pairs.size();) {
        if (pairs[index].code == 0 && pairs[index].value == "SECTION" &&
            index + 1 < pairs.size() && pairs[index + 1].code == 2 &&
            pairs[index + 1].value == "ENTITIES") {
            entities = true;
            index += 2;
            continue;
        }
        if (entities && pairs[index].code == 0 && pairs[index].value == "ENDSEC") {
            entities = false;
            ++index;
            continue;
        }
        if (!entities || pairs[index].code != 0) { ++index; continue; }
        const std::string type = pairs[index++].value;
        std::unordered_map<int, std::string> values;
        while (index < pairs.size() && pairs[index].code != 0) {
            values.try_emplace(pairs[index].code, pairs[index].value);
            ++index;
        }
        const bool construction = values.contains(8) && values.at(8) == "CONSTRUCTION";
        try {
            if (type == "LINE") {
                const auto first = point_id(
                    number(values, 10) * ambiguous_unit_scale_to_mm,
                    number(values, 20) * ambiguous_unit_scale_to_mm);
                const auto second = point_id(
                    number(values, 11) * ambiguous_unit_scale_to_mm,
                    number(values, 21) * ambiguous_unit_scale_to_mm);
                auto segment = zima::sketcher::Sketch::create_segment(
                    first, second, construction);
                const auto id = segment.id;
                target.segments.push_back(std::move(segment));
                geometry_ids.push_back(id);
            } else if (type == "CIRCLE") {
                geometry_ids.push_back(target.add_circle(
                    number(values, 10) * ambiguous_unit_scale_to_mm,
                    number(values, 20) * ambiguous_unit_scale_to_mm,
                    number(values, 40) * ambiguous_unit_scale_to_mm, construction));
            } else if (type == "ARC") {
                constexpr double radians = 3.14159265358979323846 / 180.0;
                const double cx = number(values, 10) * ambiguous_unit_scale_to_mm;
                const double cy = number(values, 20) * ambiguous_unit_scale_to_mm;
                const double radius = number(values, 40) * ambiguous_unit_scale_to_mm;
                const double start = number(values, 50) * radians;
                const double end = number(values, 51) * radians;
                geometry_ids.push_back(target.add_arc(cx, cy,
                    cx + radius * std::cos(start), cy + radius * std::sin(start),
                    cx + radius * std::cos(end), cy + radius * std::sin(end),
                    construction));
            } else if (type != "EOF" && type != "ENDSEC") {
                result.warnings.push_back("Nepodporovaná DXF entita: " + type);
                continue;
            } else continue;
            ++result.imported_entities;
        } catch (const std::exception& error) {
            result.warnings.push_back(type + ": " + error.what());
        }
    }
    if (!geometry_ids.empty()) {
        std::vector<std::string> point_ids;
        const auto add_point = [&](const std::string& point_id) {
            if (std::ranges::find(point_ids, point_id) == point_ids.end()) {
                point_ids.push_back(point_id);
            }
        };
        for (const auto& id : geometry_ids) {
            for (const auto& segment : target.segments) if (segment.id == id) {
                add_point(segment.first_point_id); add_point(segment.second_point_id);
            }
            for (const auto& circle : target.circles) if (circle.id == id) {
                add_point(circle.center_point_id);
            }
            for (const auto& arc : target.arcs) if (arc.id == id) {
                add_point(arc.center_point_id); add_point(arc.start_point_id);
                add_point(arc.end_point_id);
            }
        }
        result.import_block_id = target.add_import_block(
            path.stem().string(), path.generic_string(),
            std::move(geometry_ids), std::move(point_ids));
    }
    target.validate();
    return result;
}

void export_dxf(
    const std::filesystem::path& path, const zima::sketcher::Sketch& sketch) {
    sketch.validate();
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Nelze vytvořit DXF soubor");
    output << std::setprecision(17);
    pair(output, 0, "SECTION"); pair(output, 2, "HEADER");
    pair(output, 9, "$INSUNITS"); pair(output, 70, 4);
    pair(output, 0, "ENDSEC"); pair(output, 0, "SECTION"); pair(output, 2, "ENTITIES");
    for (const auto& segment : sketch.segments) {
        const auto* first = sketch.find_point(segment.first_point_id);
        const auto* second = sketch.find_point(segment.second_point_id);
        pair(output, 0, "LINE"); pair(output, 8, segment.construction ? "CONSTRUCTION" : "PROFILE");
        pair(output, 10, first->x); pair(output, 20, first->y);
        pair(output, 11, second->x); pair(output, 21, second->y);
    }
    for (const auto& circle : sketch.circles) {
        const auto* center = sketch.find_point(circle.center_point_id);
        pair(output, 0, "CIRCLE"); pair(output, 8, circle.construction ? "CONSTRUCTION" : "PROFILE");
        pair(output, 10, center->x); pair(output, 20, center->y); pair(output, 40, circle.radius);
    }
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    for (const auto& arc : sketch.arcs) {
        const auto* center = sketch.find_point(arc.center_point_id);
        pair(output, 0, "ARC"); pair(output, 8, arc.construction ? "CONSTRUCTION" : "PROFILE");
        pair(output, 10, center->x); pair(output, 20, center->y); pair(output, 40, arc.radius);
        pair(output, 50, arc.start_angle * degrees); pair(output, 51, arc.end_angle * degrees);
    }
    pair(output, 0, "ENDSEC"); pair(output, 0, "EOF");
    if (!output) throw std::runtime_error("Zápis DXF souboru selhal");
}

}  // namespace zima::interchange
