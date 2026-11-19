#include <zima/sketcher/curve_fillet.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace zima::sketcher {
namespace {
using V=kernel::Vec3;
double distance(V a,V b){return std::hypot(a.x-b.x,a.y-b.y);}
double cross(V a,V b){return a.x*b.y-a.y*b.x;}
V subtract(V a,V b){return {a.x-b.x,a.y-b.y,0};}
std::string point(Sketch& s,V p) {
    auto value=Sketch::create_point(p.x,p.y);const auto id=value.id;
    s.points.push_back(std::move(value));return id;
}
double unwrapped(double angle,double start) {
    while(angle<start-1e-10)angle+=2*std::numbers::pi;
    while(angle>start+2*std::numbers::pi)angle-=2*std::numbers::pi;
    return angle;
}
}
std::optional<std::array<std::string,2>> fillet_curve_endpoints(const Sketch& s,const std::string& id) {
    for(const auto& c:s.segments)if(c.id==id)return {{c.first_point_id,c.second_point_id}};
    for(const auto& c:s.arcs)if(c.id==id)return {{c.start_point_id,c.end_point_id}};
    for(const auto& c:s.elliptical_arcs)if(c.id==id)return {{c.start_point_id,c.end_point_id}};
    for(const auto& c:s.bsplines)if(c.id==id&&!c.closed&&c.control_point_ids.size()>1)
        return {{c.control_point_ids.front(),c.control_point_ids.back()}};
    return {};
}
CurveFilletGeometry curve_fillet_geometry(const Sketch& s,const std::string& first,
        const std::string& second,double radius,const std::string& vertex) {
    if(first==second||!std::isfinite(radius)||radius<=1e-9)
        throw std::invalid_argument("Corner fillet input is invalid");
    const auto a=fillet_curve_endpoints(s,first),b=fillet_curve_endpoints(s,second);
    if(!a||!b)throw std::invalid_argument("Corner fillet input is invalid");
    const std::array curves{sketch_curve_geometry(s,first),sketch_curve_geometry(s,second)};
    const std::array ends{*a,*b};
    std::array<bool,2> at_start{};
    V focus{};
    if(!vertex.empty()) {
        const auto* p=s.find_point(vertex);
        if(!p)throw std::invalid_argument("Corner fillet input is invalid");
        focus={p->x,p->y,0};
        for(std::size_t i=0;i<2;++i) {
            if(ends[i][0]!=vertex&&ends[i][1]!=vertex)
                throw std::invalid_argument("Corner fillet segments do not share a vertex");
            at_start[i]=ends[i][0]==vertex;
        }
    } else {
        double best=INFINITY;
        for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<2;++j) {
            const auto p=kernel::bspline_value(curves[0],double(i)),q=kernel::bspline_value(curves[1],double(j));
            if(const auto d=distance(p,q);d<best) {
                best=d;focus={(p.x+q.x)*.5,(p.y+q.y)*.5,0};at_start={i==0,j==0};
            }
        }
    }
    std::optional<CurveFilletGeometry> best;double score=INFINITY;
    // Multiple seeds find the local offset intersections, including concave
    // contacts. Newton only locates parameters; acceptance uses exact curves.
    for(double sa:{-1.,1.})for(double sb:{-1.,1.})
        for(double ta:{.015,.1,.35,.65,.9,.985})for(double tb:{.015,.1,.35,.65,.9,.985}) {
        double t=ta,u=tb;
        try {
            const auto offset=[&](std::size_t i,double p){return curve_offset_point(curves[i],p,radius*(i?sb:sa));};
            for(unsigned step=0;step<32;++step) {
                const auto p=offset(0,t),q=offset(1,u),res=subtract(p,q);
                const double error=distance(p,q);
                if(error<1e-9)break;
                const auto derivative=[&](std::size_t i,double parameter) {
                    const double lo=std::max(0.,parameter-1e-6),hi=std::min(1.,parameter+1e-6);
                    const auto delta=subtract(offset(i,hi),offset(i,lo));
                    return V{delta.x/(hi-lo),delta.y/(hi-lo),0};
                };
                const auto d=derivative(0,t),e=derivative(1,u);const double determinant=cross(d,e);
                if(std::abs(determinant)<1e-14)break;
                const double dt=-cross(res,e)/determinant,du=-cross(res,d)/determinant;
                bool improved{};
                for(double scale=1.;scale>=1./1024;scale*=.5) {
                    const double nt=t+scale*dt,nu=u+scale*du;
                    if(nt<=0||nt>=1||nu<=0||nu>=1)continue;
                    if(distance(offset(0,nt),offset(1,nu))>=error)continue;
                    t=nt;u=nu;improved=true;break;
                }
                if(!improved)break;
            }
            const auto center=offset(0,t);
            if(distance(center,offset(1,u))>1e-8||t<1e-8||t>1-1e-8||u<1e-8||u>1-1e-8)continue;
            const std::array contacts{kernel::bspline_value(curves[0],t),kernel::bspline_value(curves[1],u)};
            if(distance(contacts[0],contacts[1])<1e-8||
                    std::abs(distance(center,contacts[0])-radius)>1e-8||
                    std::abs(distance(center,contacts[1])-radius)>1e-8)continue;
            const double candidate=distance(contacts[0],focus)+distance(contacts[1],focus);
            if(candidate<score-1e-10){score=candidate;best=CurveFilletGeometry{center,contacts,{t,u},at_start};}
        } catch(const std::invalid_argument&) { /* Singular local support. */ }
    }
    if(!best)throw std::invalid_argument("Corner fillet radius is too large");
    return *best;
}

CornerFilletResult fillet_sketch_curves(Sketch& source,const std::string& first,
        const std::string& second,double radius,bool derived) {
    const auto a=fillet_curve_endpoints(source,first),b=fillet_curve_endpoints(source,second);
    if(!a||!b)throw std::invalid_argument("Corner fillet input is invalid");
    std::string shared;
    if (!derived) for (const auto& id : {first,second}) {
        if (std::ranges::any_of(source.import_blocks,[&](const auto& block){
                return block.external_profile && std::ranges::find(block.geometry_ids,id)!=block.geometry_ids.end(); }) ||
            std::ranges::any_of(source.bsplines,[&](const auto& spline){
                return spline.id==id && source.bspline_properties_read_only(id); }))
            throw std::invalid_argument("Fillet requires editable native curves.");
    }
    for(const auto& id:*a)if(id==(*b)[0]||id==(*b)[1]){shared=id;break;}
    if(derived&&shared.empty())throw std::invalid_argument("Corner fillet segments do not share a vertex");
    if(!derived&&std::ranges::any_of(source.corner_radii,[&](const auto& c){return
        c.first_segment_id==first||c.second_segment_id==first||c.first_segment_id==second||c.second_segment_id==second;}))
        throw std::invalid_argument("Corner fillet input is invalid");
    const auto geometry=curve_fillet_geometry(source,first,second,radius,shared);
    auto next=source;CornerFilletResult result;
    if (derived) {
        // The disposable profile contains already evaluated native geometry.
        // Its source dependencies remain exclusively in the authored Sketch.
        std::erase_if(next.curve_trims,[&](const auto& c){return c.id==first||c.id==second;});
        std::erase_if(next.offsets,[&](const auto& c){return c.id==first||c.id==second;});
    }
    std::array<std::string,2> contacts;
    const std::array ids{first,second};
    const std::array endpoints{*a,*b};
    for(std::size_t i=0;i<2;++i) {
        const auto& id=ids[i];const auto p=geometry.contacts[i];
        contacts[i]=point(next,p);
        const auto old=geometry.trim_start[i]?endpoints[i][0]:endpoints[i][1];
        if(auto c=std::ranges::find(next.segments,id,&SketchSegment::id);c!=next.segments.end()) {
            (geometry.trim_start[i]?c->first_point_id:c->second_point_id)=contacts[i];
            for(auto& constraint:next.constraints)if(constraint.geometry_id==id&&
                    (constraint.kind==ConstraintKind::Horizontal||constraint.kind==ConstraintKind::Vertical)) {
                constraint.first_point_id=c->first_point_id;constraint.second_point_id=c->second_point_id;
            }
        } else if(auto c=std::ranges::find(next.arcs,id,&SketchArc::id);c!=next.arcs.end()) {
            const auto* center=next.find_point(c->center_point_id);
            const double angle=unwrapped(std::atan2(p.y-center->y,p.x-center->x),c->start_angle);
            (geometry.trim_start[i]?c->start_angle:c->end_angle)=angle;
            (geometry.trim_start[i]?c->start_point_id:c->end_point_id)=contacts[i];
        } else if(auto c=std::ranges::find(next.elliptical_arcs,id,&SketchEllipticalArc::id);c!=next.elliptical_arcs.end()) {
            const auto* center=next.find_point(c->center_point_id);
            const double x=p.x-center->x,y=p.y-center->y;
            const double angle=unwrapped(std::atan2((c->reversed?-1.:1.)*(-x*std::sin(c->rotation)+y*std::cos(c->rotation))/c->minor_radius,
                (x*std::cos(c->rotation)+y*std::sin(c->rotation))/c->major_radius),c->start_parameter);
            (geometry.trim_start[i]?c->start_parameter:c->end_parameter)=angle;
            (geometry.trim_start[i]?c->start_point_id:c->end_point_id)=contacts[i];
        } else {
            const auto curve=trim_curve_geometry(sketch_curve_geometry(source,id),geometry.trim_start[i]?geometry.parameters[i]:0.,
                geometry.trim_start[i]?1.:geometry.parameters[i]);
            auto spline=std::ranges::find(next.bsplines,id,&SketchBSpline::id);
            // Retain the untrimmed endpoint identity and native exact spline.
            // Interior controls are reconstructed by exact knot insertion;
            // their old independently referenced points remain in the Sketch.
            std::vector<std::string> controls;
            for(std::size_t j=0;j<curve.poles.size();++j) {
                if(j==0)controls.push_back(geometry.trim_start[i]?contacts[i]:endpoints[i][0]);
                else if(j+1==curve.poles.size())controls.push_back(geometry.trim_start[i]?endpoints[i][1]:contacts[i]);
                else controls.push_back(point(next,curve.poles[j]));
            }
            spline->control_point_ids=std::move(controls);spline->degree=curve.degree;spline->knots=curve.knots;spline->weights=curve.weights;spline->interpolating=false;
        }
        // Only endpoint-bearing dimensions follow the trimmed curve. Other
        // point constraints retain their explicitly authored point identity.
        for(auto& d:next.dimensions)if(d.geometry_id==id&&
                (d.kind==DimensionKind::Distance||d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY)) {
            if(d.first_point_id==old)d.first_point_id=contacts[i];
            if(d.second_point_id==old)d.second_point_id=contacts[i];
        }
    }
    const auto u=subtract(geometry.contacts[0],geometry.center),v=subtract(geometry.contacts[1],geometry.center);
    const bool reverse=cross(u,v)<0;
    const auto p=geometry.contacts[reverse?1:0],q=geometry.contacts[reverse?0:1];
    result.arc_id=next.add_arc(geometry.center.x,geometry.center.y,p.x,p.y,q.x,q.y,false,1e-8);
    auto& inserted=*std::ranges::find(next.arcs,result.arc_id,&SketchArc::id);
    inserted.start_point_id=contacts[reverse?1:0];
    inserted.end_point_id=contacts[reverse?0:1];
    result.first_tangent_point_id=contacts[0];result.second_tangent_point_id=contacts[1];
    for(std::size_t i=0;i<2;++i) {
        SketchConstraint tangent;tangent.id=Sketch::create_point(0,0).id;tangent.kind=ConstraintKind::Tangent;
        tangent.first_point_id=contacts[i];tangent.geometry_id=ids[i];tangent.second_geometry_id=result.arc_id;
        if (const auto support=std::ranges::find(next.arcs,ids[i],&SketchArc::id);support!=next.arcs.end()) {
            const auto* c=next.find_point(support->center_point_id);
            const auto p=geometry.contacts[i];
            tangent.tangent_internal=(c->x-p.x)*(geometry.center.x-p.x)+(c->y-p.y)*(geometry.center.y-p.y)>0;
        }
        next.constraints.push_back(std::move(tangent));
    }
    next.validate();
    if(!derived) {
        const auto solved=next.solve();
        if(solved.status==SolveStatus::Conflicting||solved.status==SolveStatus::Invalid)
            throw std::runtime_error("Corner fillet conflicts with existing Sketch geometry");
    }
    source=std::move(next);return result;
}
}
