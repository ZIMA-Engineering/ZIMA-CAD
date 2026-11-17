#pragma once

// Metric development of analytic support strips for the draft wire. This
// consumes the calculated packet and never builds or queries a native solid.
namespace support_chamfer_preview {
using Point=zima::kernel::Vec3;
using Surface=zima::kernel::SurfaceGeometry;
using Pair=std::array<double,2>;
double cross(Pair a,Pair b){return a[0]*b[1]-a[1]*b[0];}
Pair difference(Pair a,Pair b){return {a[0]-b[0],a[1]-b[1]};}
double norm(Pair a){return std::hypot(a[0],a[1]);}
Pair unit(Pair a){const auto n=norm(a);return {a[0]/n,a[1]/n};}
bool same(const Surface& a,const Surface& b,const Point& p) {
    if(a.kind!=b.kind||std::abs(edge_preview_dot(a.axis,b.axis))<1.-1.e-9)return false;
    const auto delta=edge_preview_difference(a.origin,b.origin);
    const bool coincident=a.kind==Surface::Kind::Plane
        ?std::abs(edge_preview_dot(delta,a.axis))<1.e-7
        :std::abs(a.radius-b.radius)<1.e-7&&edge_preview_length(
            edge_preview_offset(delta,a.axis,-edge_preview_dot(delta,a.axis)))<1.e-7;
    return coincident&&edge_preview_dot(rolling_preview_normal_distance(a,p).first,
        rolling_preview_normal_distance(b,p).first)>1.-1.e-9;
}
struct Chart {
    std::shared_ptr<const Surface> surface;
    Point x,y;
    double sign{1.},reference{},cosine{1.},sine{};
    Pair translation{};
    std::size_t guide{};
    struct Boundary {Pair first,last,inward_first,inward_last;};
    std::vector<Boundary> boundaries;
    Pair map(const Point& point) const {
        const auto d=edge_preview_difference(point,surface->origin);
        double a=edge_preview_dot(d,x),b=edge_preview_dot(d,y);
        if(surface->kind==Surface::Kind::Cylinder) {
            auto angle=std::atan2(b,a);
            angle+=2.*std::numbers::pi*std::round((reference-angle)/(2.*std::numbers::pi));
            a=sign*surface->radius*angle;b=edge_preview_dot(d,surface->axis);
        }
        return {cosine*a-sine*b+translation[0],sine*a+cosine*b+translation[1]};
    }
    Pair tangent(const Point& p,const Point& d) const {
        auto u=x,v=y;
        if(surface->kind==Surface::Kind::Cylinder) {
            auto radial=edge_preview_difference(p,surface->origin);
            radial=edge_preview_offset(radial,surface->axis,-edge_preview_dot(radial,surface->axis));
            u=edge_preview_offset({},zima::kernel::dimension_cross(surface->axis,
                *edge_preview_normalized(radial)),sign);v=surface->axis;
        }
        const auto a=edge_preview_dot(d,u),b=edge_preview_dot(d,v);
        return {cosine*a-sine*b,sine*a+cosine*b};
    }
    Point lift(Pair q) const {
        q=difference(q,translation);
        const double a=cosine*q[0]+sine*q[1],b=-sine*q[0]+cosine*q[1];
        if(surface->kind==Surface::Kind::Plane)
            return edge_preview_offset(edge_preview_offset(surface->origin,x,a),y,b);
        const auto angle=a/(sign*surface->radius);
        return edge_preview_offset(edge_preview_offset(edge_preview_offset(surface->origin,
            x,surface->radius*std::cos(angle)),y,surface->radius*std::sin(angle)),surface->axis,b);
    }
    Point lift_tangent(const Point& p,Pair d) const {
        const auto a=cosine*d[0]+sine*d[1],b=-sine*d[0]+cosine*d[1];
        if(surface->kind==Surface::Kind::Plane)return edge_preview_offset(edge_preview_offset({},x,a),y,b);
        auto radial=edge_preview_difference(p,surface->origin);
        radial=edge_preview_offset(radial,surface->axis,-edge_preview_dot(radial,surface->axis));
        return edge_preview_offset(edge_preview_offset({},zima::kernel::dimension_cross(surface->axis,
            *edge_preview_normalized(radial)),sign*a),surface->axis,b);
    }
    bool contains(Pair q) const {
        if(norm(difference(map(lift(q)),q))>1.e-6)return false;
        double nearest=1.e100,signed_distance=-1.e100;
        const double period=surface->kind==Surface::Kind::Cylinder?2.*std::numbers::pi*surface->radius:0.;
        const Pair periodic_axis{cosine*sign,sine*sign};
        const auto unwrap=[&](Pair p,Pair anchor) {
            if(period==0.)return p;
            const auto d=difference(anchor,p);
            const auto shift=period*std::round((d[0]*periodic_axis[0]+d[1]*periodic_axis[1])/period);
            return Pair{p[0]+shift*periodic_axis[0],p[1]+shift*periodic_axis[1]};
        };
        for(const auto& boundary:boundaries) {
            auto a=unwrap(boundary.first,q),b=unwrap(boundary.last,a);
            const auto d=difference(b,a);const double length2=d[0]*d[0]+d[1]*d[1];if(length2<1.e-20)continue;
            const auto v=difference(q,a);const auto t=std::clamp((v[0]*d[0]+v[1]*d[1])/length2,0.,1.);
            const Pair delta{v[0]-t*d[0],v[1]-t*d[1]};const double distance=norm(delta);
            if(distance>nearest+1.e-9)continue;
            const Pair inward{(1.-t)*boundary.inward_first[0]+t*boundary.inward_last[0],
                (1.-t)*boundary.inward_first[1]+t*boundary.inward_last[1]};
            const double sign_distance=delta[0]*inward[0]+delta[1]*inward[1];
            if(distance<nearest-1.e-9)signed_distance=sign_distance;
            else signed_distance=std::min(signed_distance,sign_distance);
            nearest=distance;
        }
        return signed_distance>=-1.e-7;
    }
};
using Atlas=std::array<std::vector<Chart>,2>;
std::optional<Atlas> prepare(const EdgeTreatmentPreviewPath& path,
        const zima::kernel::ViewerMesh& mesh,const zima::kernel::ViewerEdge& anchor,bool flip) {
    const auto count=path.edges.size();if(count<2)return {};
    std::vector<std::array<std::shared_ptr<const Surface>,2>> supports(count);
    std::size_t seed{};bool curved{};
    for(std::size_t i=0;i<count;++i) {
        const auto& edge=path.edges[i];
        if(edge.edge_treatment_side_references.size()!=2)return {};
        if(edge.reference==anchor.reference)seed=i;
        for(std::size_t side=0;side<2;++side) {
            auto surface=edge_preview_surface(mesh,edge.edge_treatment_side_references[side]);
            if(!surface||(surface->kind!=Surface::Kind::Plane&&surface->kind!=Surface::Kind::Cylinder))return {};
            curved=curved||surface->kind==Surface::Kind::Cylinder;
            supports[i][side]=std::move(surface);
        }
    }
    if(!curved)return {};
    if(flip)std::swap(supports[seed][0],supports[seed][1]);
    for(std::size_t step=1;step<count;++step) {
        const auto i=(seed+step)%count,previous=(i+count-1)%count;
        const auto& p=path.edges[i].points.front();
        if(same(*supports[previous][0],*supports[i][1],p)||
           same(*supports[previous][1],*supports[i][0],p))std::swap(supports[i][0],supports[i][1]);
        if(edge_preview_dot(rolling_preview_normal_distance(*supports[previous][0],p).first,
                rolling_preview_normal_distance(*supports[i][0],p).first)<1.-1.e-7||
           edge_preview_dot(rolling_preview_normal_distance(*supports[previous][1],p).first,
                rolling_preview_normal_distance(*supports[i][1],p).first)<1.-1.e-7)return {};
    }
    Atlas result;
    for(std::size_t side=0;side<2;++side)for(std::size_t step=0;step<3*count;++step) {
        const auto i=step%count;
        Chart chart;chart.surface=supports[i][side];const auto& surface=*chart.surface;
        chart.x=surface.radial;chart.y=zima::kernel::dimension_cross(surface.axis,chart.x);
        if(surface.kind==Surface::Kind::Plane&&surface.reversed)chart.y=edge_preview_offset({},chart.y,-1.);
        chart.sign=surface.reversed?-1.:1.;
        const auto& edge=path.edges[i];const auto& p=edge.points.front();
        if(same(surface,*edge_preview_surface(mesh,edge.edge_treatment_side_references[1]),p))chart.guide=1;
        const auto delta=edge_preview_difference(edge.points[edge.points.size()/2],surface.origin);
        chart.reference=std::atan2(edge_preview_dot(delta,chart.y),edge_preview_dot(delta,chart.x));
        if(step) {
            const auto tangent=zima::kernel::dimension_cross(
                rolling_preview_normal_distance(*supports[i][0],p).first,
                rolling_preview_normal_distance(*supports[i][1],p).first);
            if(edge_preview_length(tangent)<1.e-10)return {};
            const auto old_t=unit(result[side].back().tangent(p,tangent)),new_t=unit(chart.tangent(p,tangent));
            chart.cosine=new_t[0]*old_t[0]+new_t[1]*old_t[1];chart.sine=cross(new_t,old_t);
            chart.translation=difference(result[side].back().map(p),chart.map(p));
        }
        result[side].push_back(std::move(chart));
    }
    // Use the current face's complete trim, including holes and nonconvex
    // outlines. Two extended seam half-planes cannot describe a stepped lip.
    for(std::size_t side=0;side<2;++side)for(std::size_t index=0;index<3*count;++index) {
        const auto i=index%count;
        auto& chart=result[side][index];const auto& edge=path.edges[i];
        const auto& identity=edge.edge_treatment_side_references[chart.guide];
        for(const auto& boundary:mesh.edges)for(std::size_t side_index=0;side_index<boundary.edge_treatment_side_references.size();++side_index) {
            if(boundary.edge_treatment_side_references[side_index]!=identity||
               side_index>=boundary.edge_treatment_side_directions.size()||
               boundary.edge_treatment_side_directions[side_index].size()!=boundary.points.size())continue;
            const auto& guides=boundary.edge_treatment_side_directions[side_index];
            for(std::size_t n=1;n<boundary.points.size();++n) {
                const auto& a=boundary.points[n-1];const auto& b=boundary.points[n];
                if(std::abs(rolling_preview_normal_distance(*chart.surface,a).second)>1.e-6||
                   std::abs(rolling_preview_normal_distance(*chart.surface,b).second)>1.e-6)continue;
                const auto inward_a=chart.tangent(a,guides[n-1]),inward_b=chart.tangent(b,guides[n]);
                if(norm(inward_a)<1.e-10||norm(inward_b)<1.e-10)continue;
                chart.boundaries.push_back({chart.map(a),chart.map(b),unit(inward_a),unit(inward_b)});
            }
        }
        if(chart.boundaries.empty())return {};
    }
    return result;
}
std::optional<EdgeTreatmentSection> section(const Atlas& atlas,std::size_t member,
        const zima::kernel::ViewerEdge& edge,std::size_t point_index,
        const zima::document::EdgeTreatmentParameters& parameters) {
    const auto p=edge.points[point_index];const auto count=atlas[0].size();
    member+=count/3;
    const auto n0=rolling_preview_normal_distance(*atlas[0][member].surface,p).first;
    const auto n1=rolling_preview_normal_distance(*atlas[1][member].surface,p).first;
    const auto tangent=edge_preview_normalized(zima::kernel::dimension_cross(n0,n1));if(!tangent)return {};
    std::array<Point,2> contacts;std::array<std::size_t,2> chosen;
    std::array<Pair,2> flat;
    using Mode=zima::document::EdgeTreatmentParameters::ChamferMode;
    for(std::size_t side=0;side<2;++side) {
        const auto& own=atlas[side][member];const auto& surface=*own.surface;
        auto normal=rolling_preview_normal_distance(surface,p).first;
        auto inward=zima::kernel::dimension_cross(normal,*tangent);
        if(edge_preview_dot(inward,edge.edge_treatment_side_directions[own.guide][point_index])<0.)inward=edge_preview_offset({},inward,-1.);
        const auto d=unit(own.tangent(p,inward));const auto q=own.map(p);
        const auto distance=side&&parameters.chamfer_mode==Mode::TwoDistances?parameters.secondary_size:parameters.primary_size;
        flat[side]={q[0]+distance*d[0],q[1]+distance*d[1]};
        bool found{};
        for(std::size_t shift=0;shift<count;++shift) {
            const auto k=(member+shift)%count;
            if(!atlas[side][k].contains(flat[side]))continue;
            const auto candidate=atlas[side][k].lift(flat[side]);
            if(edge_preview_length(edge_preview_difference(candidate,p))>distance+1.e-6)continue;
            contacts[side]=candidate;chosen[side]=k;found=true;break;
        }
        if(!found)return {};
    }
    if(parameters.chamfer_mode==Mode::DistanceAngle) {
        const auto& chart=atlas[0][chosen[0]];const auto a=contacts[0];
        const auto normal=rolling_preview_normal_distance(*chart.surface,a).first;
        auto t=chart.lift_tangent(a,atlas[0][member].tangent(p,*tangent));
        auto inward=zima::kernel::dimension_cross(normal,*edge_preview_normalized(t));
        if(edge_preview_dot(inward,edge_preview_difference(a,p))<0.)inward=edge_preview_offset({},inward,-1.);
        const auto sign=edge_preview_dot(edge_preview_difference(contacts[1],p),normal)>0.?1.:-1.;
        const double angle=parameters.angle_degrees*std::numbers::pi/180.;
        const auto ray=edge_preview_offset(edge_preview_offset({},inward,-std::cos(angle)),normal,sign*std::sin(angle));
        double nearest=1.e100,gap=1.e100;std::optional<Point> hit;
        for(const auto& opposite:atlas[1]) {
            const auto& s=*opposite.surface;const auto delta=edge_preview_difference(a,s.origin);
            std::vector<double> roots;
            if(s.kind==Surface::Kind::Plane) {
                const auto divisor=edge_preview_dot(ray,s.axis);
                if(std::abs(divisor)>1.e-12)roots.push_back(-edge_preview_dot(delta,s.axis)/divisor);
            } else {
                const auto d=edge_preview_offset(delta,s.axis,-edge_preview_dot(delta,s.axis));
                const auto r=edge_preview_offset(ray,s.axis,-edge_preview_dot(ray,s.axis));
                const auto aa=edge_preview_dot(r,r),b=edge_preview_dot(d,r);
                const auto disc=b*b-aa*(edge_preview_dot(d,d)-s.radius*s.radius);
                if(aa>1.e-12&&disc>=0.){roots.push_back((-b-std::sqrt(disc))/aa);roots.push_back((-b+std::sqrt(disc))/aa);}
            }
            for(const auto distance:roots)if(distance>1.e-7) {
                const auto point=edge_preview_offset(a,ray,distance);
                const auto q=opposite.map(point);
                if(!opposite.contains(q))continue;
                const auto flat_gap=norm(difference(q,flat[1]));
                if(distance<nearest-1.e-7||(std::abs(distance-nearest)<1.e-7&&flat_gap<gap)) {
                    nearest=distance;gap=flat_gap;hit=point;
                }
            }
        }
        if(!hit)return {};contacts[1]=*hit;
    }
    EdgeTreatmentSection result;result.wire.overlay=true;result.wire.points={contacts[0],contacts[1]};
    result.first_tangent=contacts[0];result.second_tangent=contacts[1];
    result.first_distance=parameters.primary_size;result.second_distance=parameters.secondary_size;
    result.plane_normal=*tangent;result.curved_support=true;
    return result;
}
} // namespace support_chamfer_preview
