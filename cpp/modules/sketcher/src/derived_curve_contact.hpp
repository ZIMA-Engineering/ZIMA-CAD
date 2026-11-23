#pragma once
#include "exact_external_circle.hpp"
#include <zima/kernel/curve_evaluation.hpp>
#include <set>
#include <limits>

namespace zima::sketcher::detail {
struct DerivedCurveEvaluationCache {
    struct Offset {kernel::BSplineGeometry source,result;double distance,tolerance;bool flipped;};
    struct Piece {kernel::BSplineGeometry curve;double first,last,min_x,min_y,max_x,max_y;};
    struct Projection {kernel::BSplineGeometry source;std::vector<Piece> pieces;};
    std::vector<Offset> offsets;
    std::vector<Projection> projections;
};
inline thread_local DerivedCurveEvaluationCache* derived_curve_evaluation_cache{};
// A solve owns this bounded disposable cache. Keys compare complete current
// input geometry and offset side/tolerance; no revision or display cache is used.
struct DerivedCurveEvaluationScope {
    DerivedCurveEvaluationCache cache;
    bool owns{derived_curve_evaluation_cache==nullptr};
    DerivedCurveEvaluationScope(){if(owns)derived_curve_evaluation_cache=&cache;}
    ~DerivedCurveEvaluationScope(){if(owns)derived_curve_evaluation_cache=nullptr;}
    DerivedCurveEvaluationScope(const DerivedCurveEvaluationScope&)=delete;
    DerivedCurveEvaluationScope& operator=(const DerivedCurveEvaluationScope&)=delete;
};
inline kernel::BSplineGeometry derived_support(
    const Sketch& sketch,const std::string& id,std::set<std::string>& visiting) {
    if(!visiting.insert(id).second)throw std::invalid_argument("Cyclic Sketch curve dependency");
    const auto* offset=sketch.find_offset(id);
    if(!offset) {
        visiting.erase(id);
        // Ellipse axes are native point variables. The DOF Jacobian perturbs
        // those points without rewriting cached radius/rotation members.
        // Reuse the exact rational conic builder with their current frame.
        const auto conic=[&](const auto& ellipse,double first,double last) {
            const auto* center=sketch.find_point(ellipse.center_point_id);
            const auto* major=sketch.find_point(ellipse.major_point_id);
            const auto* minor=sketch.find_point(ellipse.minor_point_id);
            const double ax=major->x-center->x,ay=major->y-center->y;
            const double ratio=std::hypot(minor->x-center->x,minor->y-center->y)/std::hypot(ax,ay)*(ellipse.reversed?-1:1);
            auto curve=circular_curve_geometry({0,0,0},1,first,last);
            for(auto& p:curve.poles) {
                const double x=p.x,y=p.y;
                p.x=center->x+ax*x-ay*ratio*y;p.y=center->y+ay*x+ax*ratio*y;
            }
            return curve;
        };
        if(const auto e=std::ranges::find(sketch.ellipses,id,&SketchEllipse::id);e!=sketch.ellipses.end())
            return conic(*e,0,2*std::numbers::pi);
        if(const auto e=std::ranges::find(sketch.elliptical_arcs,id,&SketchEllipticalArc::id);e!=sketch.elliptical_arcs.end())
            return conic(*e,e->start_parameter,e->end_parameter);
        return sketch.supporting_curve(id);
    }
    const auto source=derived_support(sketch,offset->source_id,visiting);visiting.erase(id);
    auto* cache=derived_curve_evaluation_cache;
    if(cache)for(const auto& entry:cache->offsets)
        if(entry.source==source&&entry.distance==offset->distance&&entry.flipped==offset->flipped&&entry.tolerance==offset->tolerance)
            return entry.result;
    auto result=offset_curve_geometry(source,offset->flipped?-offset->distance:offset->distance,offset->tolerance);
    if(cache&&source.poles.size()<=4096&&result.poles.size()<=4096) {
        if(cache->offsets.size()>=16)cache->offsets.erase(cache->offsets.begin());
        cache->offsets.push_back({source,result,offset->distance,offset->tolerance,offset->flipped});
    }
    return result;
}
inline kernel::CurveProjection project_derived_curve(const kernel::BSplineGeometry& curve,kernel::Vec3 point) {
    // Positive rational weights keep each knot span inside its pole hull.
    // Reject spans only by that exact conservative bound, then reuse the
    // ordinary native projector on the remaining exact trimmed spans.
    DerivedCurveEvaluationScope scope;
    auto& cache=derived_curve_evaluation_cache->projections;
    auto found=std::ranges::find(cache,curve,&DerivedCurveEvaluationCache::Projection::source);
    if(found==cache.end()) {
        DerivedCurveEvaluationCache::Projection entry;entry.source=curve;
        const double low=curve.knots[curve.degree],range=curve.knots[curve.poles.size()]-low;
        for(std::size_t i=curve.degree;i<curve.poles.size();++i) {
            if(curve.knots[i+1]<=curve.knots[i])continue;
            const double a=(curve.knots[i]-low)/range,b=(curve.knots[i+1]-low)/range;
            auto piece=trim_curve_geometry(curve,a,b);
            double min_x=piece.poles.front().x,max_x=min_x,min_y=piece.poles.front().y,max_y=min_y;
            for(const auto& p:piece.poles){min_x=std::min(min_x,p.x);max_x=std::max(max_x,p.x);min_y=std::min(min_y,p.y);max_y=std::max(max_y,p.y);}
            entry.pieces.push_back({std::move(piece),a,b,min_x,min_y,max_x,max_y});
        }
        if(cache.size()>=16)cache.erase(cache.begin());cache.push_back(std::move(entry));found=std::prev(cache.end());
    }
    std::vector<std::pair<double,std::size_t>> order;order.reserve(found->pieces.size());
    for(std::size_t i=0;i<found->pieces.size();++i) {
        const auto& p=found->pieces[i];const double x=std::max({p.min_x-point.x,0.,point.x-p.max_x}),y=std::max({p.min_y-point.y,0.,point.y-p.max_y});
        order.emplace_back(x*x+y*y,i);
    }
    std::ranges::sort(order);
    kernel::CurveProjection best;best.squared_distance=std::numeric_limits<double>::infinity();
    std::vector<kernel::CurveProjection> contacts;
    for(const auto& [lower,index]:order) {
        if(lower>best.squared_distance+1e-12)break;
        const auto& piece=found->pieces[index];auto candidate=kernel::project_bspline(piece.curve,point);
        candidate.parameter=piece.first+(piece.last-piece.first)*candidate.parameter;
        const double scale=1/(piece.last-piece.first);candidate.tangent.x*=scale;candidate.tangent.y*=scale;candidate.tangent.z*=scale;
        if(candidate.squared_distance<1e-12)contacts.push_back(candidate);
        if(candidate.squared_distance<best.squared_distance)best=candidate;
    }
    if(best.squared_distance<1e-12)for(const auto& other:contacts) {
        const auto a=best.tangent,b=other.tangent;const double length=std::hypot(a.x,a.y)*std::hypot(b.x,b.y);
        if(!other.unique_tangent||length<1e-24||(a.x*b.x+a.y*b.y)/length<1-1e-6)best.unique_tangent=false;
    }
    return best;
}
// Read the current dependency, not the last installed display/control-point cache.
inline std::optional<kernel::BSplineGeometry> derived_curve_geometry(
    const Sketch& sketch, const std::string& id) {
    double first=0.,last=1.;
    if(const auto* offset=sketch.find_offset(id)) {
        if(offset->broken)return {};
        first=offset->start;last=offset->end;
    } else {
        const auto trim=std::ranges::find(sketch.curve_trims,id,&SketchCurveTrim::id);
        if(trim==sketch.curve_trims.end()||trim->broken)return {};
        first=trim->start;last=trim->end;
    }
    try {
        std::set<std::string> visiting;auto curve=derived_support(sketch,id,visiting);
        if(first!=0.||last!=1.)curve=trim_curve_geometry(curve,first,last);
        return curve;
    } catch(const std::exception&) {return {};}
}
inline bool radial_derived_contact(const Sketch& sketch,const SketchConstraint& c) {
    if(c.kind!=ConstraintKind::Tangent||c.first_point_id.empty()||c.second_point_id.empty()||
       !sketch.derived_circle_tangent_pair(c.geometry_id,c.second_geometry_id))return false;
    if(const auto circle=std::ranges::find(sketch.circles,c.second_geometry_id,&SketchCircle::id);circle!=sketch.circles.end())
        return circle->center_point_id==c.second_point_id;
    if(const auto arc=std::ranges::find(sketch.arcs,c.second_geometry_id,&SketchArc::id);arc!=sketch.arcs.end())
        return arc->center_point_id==c.second_point_id;
    return false;
}
inline bool noncircular_derived_curve(const Sketch& sketch,const std::string& id) {
    const auto curve=derived_curve_geometry(sketch,id);
    if(!curve)return false;
    if(!exact_external_circle(*curve))return true;
    // A radial contact remains the same relation when its live support becomes
    // circular. In particular, its C equation must still read live axis points.
    return std::ranges::any_of(sketch.constraints,[&](const auto& c) {
        return !c.suppressed&&c.geometry_id==id&&radial_derived_contact(sketch,c);
    });
}
struct DerivedCirclePair {
    std::string support_id,circle_id,center_id;
    double radius{};
};
inline std::optional<DerivedCirclePair> derived_circle_pair(
    const Sketch& sketch,const std::string& a,const std::string& b,bool radial_contact=false) {
    const auto native=[&](const std::string& support,const std::string& id)
        ->std::optional<DerivedCirclePair> {
        if(std::ranges::find(sketch.circles,id,&SketchCircle::id)==sketch.circles.end()&&
           std::ranges::find(sketch.arcs,id,&SketchArc::id)==sketch.arcs.end())return {};
        if(!radial_contact&&!noncircular_derived_curve(sketch,support))return {};
        if(radial_contact&&!derived_curve_geometry(sketch,support))return {};
        if(const auto c=std::ranges::find(sketch.circles,id,&SketchCircle::id);c!=sketch.circles.end())
            return DerivedCirclePair{support,id,c->center_point_id,c->radius};
        if(const auto c=std::ranges::find(sketch.arcs,id,&SketchArc::id);c!=sketch.arcs.end())
            return DerivedCirclePair{support,id,c->center_point_id,c->radius};
        return {};
    };
    if(auto pair=native(a,b))return pair;
    return native(b,a);
}
struct DerivedCircleContact {
    DerivedCirclePair pair;
    std::array<double,2> position,normal;
};
inline std::optional<DerivedCircleContact> derived_circle_contact(
    const Sketch& sketch,const SketchConstraint& constraint) {
    if(!radial_derived_contact(sketch,constraint))return {};
    const auto pair=derived_circle_pair(sketch,constraint.geometry_id,constraint.second_geometry_id,true);
    const auto* point=sketch.find_point(constraint.first_point_id);
    // The two point references explicitly define the radial vector. Preserve
    // its oriented side even if an ellipse temporarily has equal semiaxes.
    // Analytic circle/circle contacts retain their existing internal-side rule.
    if(!pair||!point||constraint.second_point_id!=pair->center_id)return {};
    const auto curve=derived_curve_geometry(sketch,pair->support_id);
    if(!curve)return {};
    const auto projected=project_derived_curve(*curve,{point->x,point->y,0});
    const double length=std::hypot(projected.tangent.x,projected.tangent.y);
    if(!projected.unique_tangent||length<1e-12)return {};
    return DerivedCircleContact{*pair,{projected.point.x,projected.point.y},
        {-projected.tangent.y/length,projected.tangent.x/length}};
}
} // namespace zima::sketcher::detail
