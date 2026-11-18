#pragma once

#include <zima/kernel/curve_evaluation.hpp>
#include <limits>
#include <optional>
#include <string_view>

namespace zima::kernel {
inline constexpr std::string_view placement_route_prefix{"placement-route:"};
inline constexpr std::string_view placement_evaluated_prefix{"placement-evaluated:"};

inline bool is_placement_route_member(std::string_view key) {
    return key.starts_with("curve:segment:") || key.starts_with("curve:rounding:") ||
        key.starts_with("segment:") || key.starts_with("arc:") ||
        key.starts_with("circle:") || key.starts_with("ellipse:") ||
        key.starts_with("elliptical_arc:") || key.starts_with("bspline:") ||
        key.starts_with("corner_radius:");
}

inline std::string placement_route_key(std::string_view member) {
    if (member.starts_with("curve:")) return std::string(placement_route_prefix) + "3d";
    return std::string(placement_route_prefix) + std::string(member);
}

struct PlacementRoutePiece {
    const ViewerEdge* edge{};
    bool reversed{};

    [[nodiscard]] Vec3 point(double parameter) const {
        const double t = reversed ? 1. - parameter : parameter;
        if (edge->exact_spline) return bspline_value(*edge->exact_spline,t);
        const auto a=edge->points.front(), b=edge->points.back();
        return {std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
    }
    [[nodiscard]] CurveProjection project(const Vec3& seed) const {
        CurveProjection projection;
        if (edge->exact_spline) {
            projection=project_bspline(*edge->exact_spline,seed);
        } else {
            const auto a=edge->points.front(), b=edge->points.back();
            const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
            const double square=d.x*d.x+d.y*d.y+d.z*d.z;
            const double t=square>0 ? std::clamp(((seed.x-a.x)*d.x+(seed.y-a.y)*d.y+
                (seed.z-a.z)*d.z)/square,0.,1.) : 0.;
            const Vec3 p{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
            const Vec3 delta{p.x-seed.x,p.y-seed.y,p.z-seed.z};
            projection={t,p,d,delta.x*delta.x+delta.y*delta.y+delta.z*delta.z,square>0};
        }
        if (reversed) {
            projection.parameter=1.-projection.parameter;
            projection.tangent={-projection.tangent.x,-projection.tangent.y,-projection.tangent.z};
        }
        return projection;
    }
    [[nodiscard]] double length(double end=1.) const {
        if (!edge->exact_spline) {
            const auto a=edge->points.front(), b=edge->points.back();
            return std::hypot(b.x-a.x,b.y-a.y,b.z-a.z)*end;
        }
        const auto& spline=*edge->exact_spline;
        const auto speed=[&](double parameter) {
            const auto d=bspline_derivative(spline,parameter);
            return std::hypot(d.x,d.y,d.z);
        };
        const auto integrate=[&](const auto& self,double a,double b,double fa,double fm,double fb,
                                        double estimate,int depth)->double {
            const double mid=(a+b)*.5, fl=speed((a+mid)*.5), fr=speed((mid+b)*.5);
            const double left=(mid-a)*(fa+4.*fl+fm)/6.;
            const double right=(b-mid)*(fm+4.*fr+fb)/6.;
            const double error=left+right-estimate;
            if (depth==0 || std::abs(error)<=1e-10*std::max(1.,left+right))
                return left+right+error/15.;
            return self(self,a,mid,fa,fl,fm,left,depth-1)+self(self,mid,b,fm,fr,fb,right,depth-1);
        };
        const double begin=reversed ? 1.-end : 0., finish=reversed ? 1. : end;
        double length=0.;
        const double first=spline.knots[spline.degree], span=spline.knots[spline.poles.size()]-first;
        for (std::size_t k=spline.degree;k<spline.poles.size();++k) {
            const double a=std::max(begin,(spline.knots[k]-first)/span);
            const double b=std::min(finish,(spline.knots[k+1]-first)/span);
            if (b<=a) continue;
            const double fa=speed(std::nextafter(a,b)),fm=speed((a+b)*.5),fb=speed(std::nextafter(b,a));
            length+=integrate(integrate,a,b,fa,fm,fb,(b-a)*(fa+4.*fm+fb)/6.,16);
        }
        return length;
    }
};

inline bool has_exact_placement_route_geometry(const ViewerEdge& edge) {
    return edge.points.size()>=2 && (edge.exact_spline.has_value() || edge.points.size()==2);
}

inline bool same_route_point(Vec3 a,Vec3 b) {
    return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z) <= 1e-7;
}

// Sketches can contain several disconnected paths. The selected authored
// member identifies its chain. An open chain follows that member's direction;
// closed chains also use it as their seam. Ambiguous branches have no route.
inline std::vector<PlacementRoutePiece> placement_route_pieces(
        const std::vector<ViewerEdge>& edges, std::string_view owner,
        std::string_view key, std::string_view instance) {
    if (!key.starts_with(placement_route_prefix)) return {};
    const auto anchor=key.substr(placement_route_prefix.size());
    const bool spatial=anchor=="3d";
    const bool evaluated=!spatial && std::ranges::any_of(edges,[&](const auto& edge) {
        return edge.reference.owner_id==owner && edge.reference.instance_path==instance &&
            edge.reference.semantic_key.starts_with(placement_evaluated_prefix);
    });
    const auto member_key=[&](const ViewerEdge& edge) {
        std::string_view key=edge.reference.semantic_key;
        if (evaluated && key.starts_with(placement_evaluated_prefix))
            key.remove_prefix(placement_evaluated_prefix.size());
        return key;
    };
    std::vector<const ViewerEdge*> available;
    for (const auto& edge : edges) {
        const auto& ref=edge.reference;
        if (ref.owner_id!=owner || ref.instance_path!=instance || !has_exact_placement_route_geometry(edge) ||
            (evaluated && !ref.semantic_key.starts_with(placement_evaluated_prefix)) ||
            !is_placement_route_member(member_key(edge)) || member_key(edge).starts_with("curve:")!=spatial)
            continue;
        if (std::ranges::none_of(available,[&](const auto* other){ return other->reference==ref; }))
            available.push_back(&edge);
    }
    if (available.empty()) return {};
    const auto first=spatial ? available.begin() : std::ranges::find_if(available,[&](const auto* edge){
        return member_key(*edge)==anchor;
    });
    if (first==available.end()) return {};
    std::vector<PlacementRoutePiece> pieces{{*first,false}};
    available.erase(first);
    for (const bool forward : {true,false}) {
        while (!available.empty() && !same_route_point(pieces.front().point(0),pieces.back().point(1))) {
            const auto endpoint=forward ? pieces.back().point(1) : pieces.front().point(0);
            auto next=available.end();
            bool reversed{};
            for (auto it=available.begin();it!=available.end();++it) {
                const PlacementRoutePiece piece{*it,false};
                const bool start=same_route_point(endpoint,piece.point(0));
                const bool end=same_route_point(endpoint,piece.point(1));
                if (!start && !end) continue;
                if (next!=available.end() || (start && end)) return {};
                next=it;
                reversed=forward ? end : start;
            }
            if (next==available.end()) break;
            if (forward) pieces.push_back({*next,reversed});
            else pieces.insert(pieces.begin(),{*next,reversed});
            available.erase(next);
        }
    }
    for (const auto* unused : available) {
        const PlacementRoutePiece piece{unused,false};
        if (spatial || std::ranges::any_of(pieces,[&](const auto& used){
                return same_route_point(piece.point(0),used.point(0)) ||
                    same_route_point(piece.point(1),used.point(0)) ||
                    same_route_point(piece.point(0),used.point(1)) ||
                    same_route_point(piece.point(1),used.point(1));
            })) return {};
    }
    return pieces;
}

struct PlacementRouteSpan {
    std::vector<double> lengths;
    double total{};
    double start{};
    bool closed{};
    [[nodiscard]] double available() const { return closed ? total : total-start; }
};

inline std::optional<PlacementRouteSpan> placement_route_span(
        const std::vector<PlacementRoutePiece>& pieces,const Vec3& anchor) {
    if (pieces.empty()) return {};
    PlacementRouteSpan span;
    double nearest=std::numeric_limits<double>::infinity();
    std::vector<double> coincident;
    span.lengths.reserve(pieces.size());
    for (const auto& piece:pieces) {
        const auto projected=piece.project(anchor);
        if (projected.squared_distance<=1e-14) {
            const double station=span.total+piece.length(projected.parameter);
            coincident.push_back(station);
            if (projected.squared_distance<nearest) {
                nearest=projected.squared_distance;
                span.start=station;
            }
        }
        span.lengths.push_back(piece.length());
        span.total+=span.lengths.back();
    }
    if (nearest>1e-14 || span.total<=1e-10) return {};
    span.closed=same_route_point(pieces.front().point(0),pieces.back().point(1));
    for (const double other:coincident) {
        const double separation=std::abs(other-span.start);
        if (separation>1e-7 && (!span.closed || std::abs(separation-span.total)>1e-7)) return {};
    }
    if (span.closed && span.start>=span.total-1e-10) span.start=0.;
    return span;
}

inline std::optional<Vec3> placement_route_station(const std::vector<PlacementRoutePiece>& pieces,
        const Vec3& anchor,double station,bool fraction) {
    if (!std::isfinite(station) || station<0. || (fraction && station>1.)) return {};
    const auto span=placement_route_span(pieces,anchor);
    if (!span) return {};
    const double available=span->available();
    const double distance=fraction ? station*available : station;
    if (distance>available+1e-8) return {};
    double target=span->start+std::min(distance,available);
    if (span->closed && target>=span->total) target-=span->total;
    const auto& lengths=span->lengths;
    for (std::size_t index=0;index<pieces.size();++index) {
        const auto& piece=pieces[index];
        if (target>lengths[index] && index+1<pieces.size()) {
            target-=lengths[index];
            continue;
        }
        if (target<=1e-10) return piece.point(0.);
        if (target>=lengths[index]-1e-10) return piece.point(1.);
        if (!piece.edge->exact_spline) return piece.point(target/lengths[index]);
        double lower=0.,upper=1.;
        for (int iteration=0;iteration<48;++iteration) {
            const double mid=(lower+upper)*.5;
            if (piece.length(mid)<target) lower=mid;
            else upper=mid;
        }
        return piece.point((lower+upper)*.5);
    }
    return {};
}
}
