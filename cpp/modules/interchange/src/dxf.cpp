#include <zima/interchange/dxf.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <unordered_set>
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
    while (std::getline(input, code)) {
        if (!std::getline(input, value)) throw std::runtime_error("Neúplný DXF pár");
        const auto trim = [](std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            text = first == std::string::npos ? std::string{} :
                text.substr(first, last - first + 1);
        };
        trim(code);
        trim(value);
        try { std::size_t end{}; const auto group = std::stoi(code, &end);
            if (end != code.size()) throw std::runtime_error("Invalid code");
            result.push_back({group, value}); }
        catch (const std::exception&) { throw std::runtime_error("Neplatný DXF group code"); }
    }
    if (!input.eof()) throw std::runtime_error("Neúplný DXF pár");
    return result;
}

double number(const std::unordered_map<int, std::string>& values, int code) {
    const auto found = values.find(code);
    if (found == values.end()) throw std::runtime_error("DXF entitě chybí souřadnice");
    std::size_t end{}; const auto value = std::stod(found->second, &end);
    if (end != found->second.size() || !std::isfinite(value)) throw std::runtime_error("Neplatné DXF číslo");
    return value;
}

int integer(const std::unordered_map<int,std::string>& values, int code) {
    const auto value = number(values,code);
    if (value != std::floor(value) || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Neplatné DXF celé číslo");
    return static_cast<int>(value);
}

void pair(std::ostream& output, int code, const auto& value) {
    output << code << '\n' << value << '\n';
}

}  // namespace

DxfImportResult import_dxf(
    const std::filesystem::path& path, zima::sketcher::Sketch& destination,
    double ambiguous_unit_scale_to_mm, std::size_t maximum_entities) {
    if (!std::isfinite(ambiguous_unit_scale_to_mm) || ambiguous_unit_scale_to_mm <= 0.0) {
        throw std::invalid_argument("Měřítko DXF musí být kladné");
    }
    if (maximum_entities == 0) {
        throw std::invalid_argument("Limit DXF entit musí být kladný");
    }
    auto target = destination;
    const auto pairs = read_pairs(path);
    DxfImportResult result;
    // $INSUNITS is authoritative; the caller's scale is only for unitless DXF.
    for (std::size_t i = 0; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code != 9 || pairs[i].value != "$INSUNITS") continue;
        if (pairs[i+1].code != 70) throw std::runtime_error("Neplatné DXF jednotky");
        const std::unordered_map<int,std::string> unit{{70,pairs[i+1].value}};
        const double value = number(unit,70);
        const std::map<int,double> scales{{1,25.4},{2,304.8},{3,1609344},{4,1},{5,10},{6,1000},
            {7,1000000},{8,0.0000254},{9,0.0254},{10,914.4},{11,1e-7},{12,1e-6},{13,0.001},
            {14,100},{15,10000},{16,100000},{17,1e12},{18,1.495978707e14},{19,9.4607304725808e18},
            {20,3.0856775814913673e19},{21,1200000.0/3937},{22,100000.0/3937},
            {23,3600000.0/3937},{24,6336000000.0/3937}};
        if (value != 0) {
            if (value != std::floor(value) || value < 1 || value > 24)
                throw std::runtime_error("Nepodporované DXF jednotky");
            ambiguous_unit_scale_to_mm = scales.at(static_cast<int>(value));
        }
        break;
    }
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
    // A new block owns its own points, even when it overlaps an older block.
    // Only points inside this import are shared.
    const auto point_id = [&](double x, double y) {
        if (x == 0) x = 0; // canonicalize signed zero for closed profiles
        if (y == 0) y = 0;
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
    struct Vertex { double x{}, y{}, bulge{}; };
    const auto polyline = [&](const std::vector<Vertex>& vertices, bool closed, bool construction) {
        if (vertices.size() < 2) throw std::runtime_error("DXF polyline nemá dva vrcholy");
        const auto count = closed ? vertices.size() : vertices.size()-1;
        for (std::size_t i=0;i<count;++i) {
            const auto& a=vertices[i]; const auto& b=vertices[(i+1)%vertices.size()];
            if (a.bulge == 0) {
                const auto first=point_id(a.x,a.y), second=point_id(b.x,b.y);
                auto segment=sketcher::Sketch::create_segment(first,second,construction);
                geometry_ids.push_back(segment.id); target.segments.push_back(std::move(segment));
            } else {
                const double dx=b.x-a.x,dy=b.y-a.y;
                if (std::hypot(dx,dy)<1e-12) throw std::runtime_error("Neplatný DXF oblouk polyline");
                const double offset=(1-a.bulge*a.bulge)/(4*a.bulge);
                const double cx=(a.x+b.x)/2-dy*offset,cy=(a.y+b.y)/2+dx*offset;
                geometry_ids.push_back(a.bulge>0 ? target.add_arc(cx,cy,a.x,a.y,b.x,b.y,construction)
                    : target.add_arc(cx,cy,b.x,b.y,a.x,a.y,construction));
            }
        }
    };
    const auto planar = [&](const std::unordered_map<int,std::string>& values) {
        for (int code : {30,31,38,39,210,220})
            if (values.contains(code) && std::abs(number(values,code))>1e-9)
                throw std::runtime_error("DXF vyžaduje rovinnou 2D geometrii v XY");
        if (values.contains(230) && std::abs(number(values,230)-1)>1e-9)
            throw std::runtime_error("Nepodporovaná orientace DXF entity");
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
        const auto first_pair = index;
        std::unordered_map<int, std::string> values;
        while (index < pairs.size() && pairs[index].code != 0) {
            values.try_emplace(pairs[index].code, pairs[index].value);
            ++index;
        }
        const bool construction = values.contains(8) && values.at(8) == "CONSTRUCTION";
        if (values.contains(67) && number(values,67) == 1) continue; // paper space
        try {
            if (type == "LINE" || type == "CIRCLE" || type == "ARC" ||
                type == "LWPOLYLINE" || type == "POLYLINE") planar(values);
            if (type == "LWPOLYLINE") {
                std::vector<Vertex> vertices;
                bool has_y = true;
                for (auto i=first_pair; i<index; ++i) {
                    if (pairs[i].code == 10) {
                        if (!has_y) throw std::runtime_error("DXF vrcholu chybí Y");
                        vertices.push_back({}); has_y = false;
                    }
                    if (pairs[i].code != 10 && pairs[i].code != 20 && pairs[i].code != 42) continue;
                    if (vertices.empty()) throw std::runtime_error("Neplatné pořadí DXF vrcholů");
                    const std::unordered_map<int,std::string> field{{pairs[i].code,pairs[i].value}};
                    const auto n=number(field,pairs[i].code);
                    if (pairs[i].code==10) vertices.back().x=n*ambiguous_unit_scale_to_mm;
                    if (pairs[i].code==20) { vertices.back().y=n*ambiguous_unit_scale_to_mm; has_y=true; }
                    if (pairs[i].code==42) vertices.back().bulge=n;
                }
                if (!has_y || !values.contains(90) || integer(values,90) != vertices.size())
                    throw std::runtime_error("Neúplná DXF polyline");
                polyline(vertices, values.contains(70) && (integer(values,70)&1), construction);
            } else if (type == "POLYLINE") {
                const int flags=values.contains(70)?integer(values,70):0;
                if (flags & (8|16|64)) throw std::runtime_error("3D/polyface DXF polyline není podporována");
                std::vector<Vertex> vertices;
                while(index<pairs.size() && pairs[index].code==0 && pairs[index].value=="VERTEX") {
                    ++index; std::unordered_map<int,std::string> v;
                    while(index<pairs.size() && pairs[index].code!=0) {
                        v.try_emplace(pairs[index].code,pairs[index].value); ++index;
                    }
                    planar(v);
                    vertices.push_back({number(v,10)*ambiguous_unit_scale_to_mm,
                        number(v,20)*ambiguous_unit_scale_to_mm,v.contains(42)?number(v,42):0});
                }
                if(index>=pairs.size() || pairs[index].value!="SEQEND") throw std::runtime_error("DXF polyline nemá SEQEND");
                ++index; while(index<pairs.size() && pairs[index].code!=0) ++index;
                polyline(vertices,(flags&1)!=0,construction);
            } else if (type == "LINE") {
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
                if (!(radius > 0)) throw std::runtime_error("Poloměr DXF musí být kladný");
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
            throw std::runtime_error(type + ": " + error.what());
        }
    }
    if (!geometry_ids.empty()) {
        std::vector<std::string> point_ids;
        std::unordered_set<std::string> seen_points;
        const std::unordered_set<std::string> imported_ids(geometry_ids.begin(),geometry_ids.end());
        const auto add_point = [&](const std::string& id) {
            if (seen_points.insert(id).second) point_ids.push_back(id);
        };
        for (const auto& segment : target.segments) if (imported_ids.contains(segment.id)) {
            add_point(segment.first_point_id); add_point(segment.second_point_id);
        }
        for (const auto& circle : target.circles) if (imported_ids.contains(circle.id)) add_point(circle.center_point_id);
        for (const auto& arc : target.arcs) if (imported_ids.contains(arc.id)) {
            add_point(arc.center_point_id); add_point(arc.start_point_id); add_point(arc.end_point_id);
        }
        result.import_block_id = target.add_import_block(
            path.stem().string(), path.generic_string(),
            std::move(geometry_ids), std::move(point_ids));
    }
    target.validate();
    destination = std::move(target);
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
