#include <zima/document/file_path.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/kernel/stable_id.hpp>
#include <numbers>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
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
    // Parse into an independent block. Native circle/arc factories may reuse
    // coincident points, but must never borrow points from an earlier import.
    auto target = sketcher::Sketch::create_default();
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
                type == "LWPOLYLINE" || type == "POLYLINE" || type == "XLINE") planar(values);
            if(type=="ELLIPSE"||type=="SPLINE") {
                auto xy=values;
                if(xy.contains(230)&&std::abs(std::abs(number(xy,230))-1)<1e-9)xy.erase(230);
                planar(xy);
            }
            if(type=="SPLINE") {
                const int degree=integer(values,71),count=integer(values,73),knot_count=integer(values,72);
                if(degree<1||count<=degree||knot_count<=0)throw std::runtime_error("DXF spline degree or array counts are invalid.");
                kernel::BSplineGeometry curve;curve.degree=static_cast<unsigned>(degree);
                std::vector<double> xs,ys,zs;
                for(auto i=first_pair;i<index;++i) {
                    const auto code=pairs[i].code;
                    if(code!=10&&code!=20&&code!=30&&code!=40&&code!=41)continue;
                    const double value=number({{code,pairs[i].value}},code);
                    if(code==10)xs.push_back(value*ambiguous_unit_scale_to_mm);
                    if(code==20)ys.push_back(value*ambiguous_unit_scale_to_mm);
                    if(code==30)zs.push_back(value*ambiguous_unit_scale_to_mm);
                    if(code==40)curve.knots.push_back(value);
                    if(code==41)curve.weights.push_back(value);
                }
                if(xs.size()!=count||ys.size()!=count||(!zs.empty()&&zs.size()!=count)||curve.knots.size()!=knot_count||(!curve.weights.empty()&&curve.weights.size()!=count))
                    throw std::runtime_error("DXF spline degree or array counts are invalid.");
                for(double z:zs)if(std::abs(z)>1e-9)throw std::runtime_error("DXF spline control points must lie in XY.");
                for(std::size_t i=0;i<xs.size();++i)curve.poles.push_back({xs[i],ys[i],0});
                if(curve.weights.empty())curve.weights.assign(xs.size(),1);
                try{curve.validate();}catch(const std::exception&){throw std::runtime_error("DXF spline control data is invalid or not clamped.");}
                const int flags=values.contains(70)?integer(values,70):0;
                const bool closed=(flags&(1|2))!=0;
                if(closed&&std::hypot(xs.front()-xs.back(),ys.front()-ys.back())>1e-8)
                    throw std::runtime_error("Closed DXF spline endpoints do not coincide.");
                sketcher::SketchBSpline spline;spline.id=kernel::make_stable_id();spline.degree=curve.degree;spline.closed=closed;spline.construction=construction;
                spline.knots=std::move(curve.knots);spline.weights=std::move(curve.weights);
                for(const auto& p:curve.poles)spline.control_point_ids.push_back(point_id(p.x,p.y));
                geometry_ids.push_back(spline.id);target.bsplines.push_back(std::move(spline));
            } else if(type=="ELLIPSE") {
                const double cx=number(values,10)*ambiguous_unit_scale_to_mm,cy=number(values,20)*ambiguous_unit_scale_to_mm;
                const double ax=number(values,11)*ambiguous_unit_scale_to_mm,ay=number(values,21)*ambiguous_unit_scale_to_mm;
                const double radius=std::hypot(ax,ay),ratio=number(values,40),sign=values.contains(230)&&number(values,230)<0?-1:1;
                const double start=number(values,41),raw_end=number(values,42),turn=2*std::numbers::pi;
                if(!(radius>0)||!(ratio>0&&ratio<=1)||std::abs(raw_end-start)>turn+1e-9)
                    throw std::runtime_error("DXF ellipse axes or parameter interval are invalid.");
                double end=raw_end;if(end<=start)end+=turn;
                const auto center_id=point_id(cx,cy),major_id=point_id(cx+ax,cy+ay),minor_id=point_id(cx-sign*ay*ratio,cy+sign*ax*ratio);
                const auto id=kernel::make_stable_id();
                if(end-start>=turn-1e-12) {
                    target.ellipses.push_back({id,center_id,major_id,minor_id,radius,radius*ratio,std::atan2(ay,ax),construction,sign<0});
                } else {
                    const auto point=[&](double t){return point_id(cx+ax*std::cos(t)-sign*ay*ratio*std::sin(t),cy+ay*std::cos(t)+sign*ax*ratio*std::sin(t));};
                    target.elliptical_arcs.push_back({id,center_id,major_id,minor_id,point(start),point(end),radius,radius*ratio,std::atan2(ay,ax),start,end,construction,sign<0});
                }
                geometry_ids.push_back(id);
            } else if(type=="XLINE") {
                const double x=number(values,10)*ambiguous_unit_scale_to_mm,y=number(values,20)*ambiguous_unit_scale_to_mm;
                const double dx=number(values,11),dy=number(values,21),length=std::hypot(dx,dy);
                if(!(length>1e-12))throw std::runtime_error("DXF axis direction is invalid.");
                auto segment=sketcher::Sketch::create_segment(point_id(x,y),point_id(x+dx/length,y+dy/length),true);segment.centerline=true;
                geometry_ids.push_back(segment.id);target.segments.push_back(std::move(segment));
            } else if (type == "LWPOLYLINE") {
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
        for(const auto& ellipse:target.ellipses) {add_point(ellipse.center_point_id);add_point(ellipse.major_point_id);add_point(ellipse.minor_point_id);}
        for(const auto& arc:target.elliptical_arcs) {add_point(arc.center_point_id);add_point(arc.major_point_id);add_point(arc.minor_point_id);add_point(arc.start_point_id);add_point(arc.end_point_id);}
        for(const auto& spline:target.bsplines)for(const auto& id:spline.control_point_ids)add_point(id);
        result.import_block_id = target.add_import_block(
            document::path_to_utf8(path.stem()), document::path_to_utf8(path),
            std::move(geometry_ids), std::move(point_ids));
    }
    target.validate();
    auto next=destination;
    const auto append=[](auto& to,auto& from){to.insert(to.end(),std::make_move_iterator(from.begin()),std::make_move_iterator(from.end()));};
    append(next.points,target.points);append(next.segments,target.segments);append(next.circles,target.circles);append(next.arcs,target.arcs);
    append(next.ellipses,target.ellipses);append(next.elliptical_arcs,target.elliptical_arcs);append(next.bsplines,target.bsplines);
    append(next.constraints,target.constraints);append(next.dimensions,target.dimensions);append(next.import_blocks,target.import_blocks);
    next.validate();destination=std::move(next);
    return result;
}

}  // namespace zima::interchange
