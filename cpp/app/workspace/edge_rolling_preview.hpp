#pragma once

// Analytic draft geometry only. All support descriptors come from the
// calculated viewer packet; this code must never request a kernel calculation.
struct RollingPreviewMember {
    std::array<std::shared_ptr<const zima::kernel::SurfaceGeometry>,2> supports;
    zima::kernel::Vec3 first_center,last_center;
    double signed_radius{};
    zima::kernel::Vec3 middle_center;
    bool trimmed_by_torus{};
};

std::shared_ptr<const zima::kernel::SurfaceGeometry> rolling_preview_support(
        const zima::kernel::ViewerMesh& mesh,const zima::kernel::FaceReference& face) {
    if(const auto surface=edge_preview_surface(mesh,face))return surface;
    // Torus descriptors belong to the contour packet, including in compact
    // rollback snapshots. Resolve that same face identity without a B-Rep read.
    const auto torus=[](const auto& reference) {
        return reference.contour_surface&&reference.contour_surface->kind==
            zima::kernel::SurfaceGeometry::Kind::Torus;
    };
    if(torus(face))return face.contour_surface;
    for(const auto* references:{&mesh.triangle_references,
            &mesh.original_references.triangle_references}) {
        const auto found=std::ranges::find_if(*references,
            [&](const auto& candidate){return candidate==face&&torus(candidate);});
        if(found!=references->end())return found->contour_surface;
    }
    return {};
}

std::pair<zima::kernel::Vec3,double> rolling_preview_normal_distance(
        const zima::kernel::SurfaceGeometry& surface,
        const zima::kernel::Vec3& point) {
    using Kind=zima::kernel::SurfaceGeometry::Kind;
    auto delta=edge_preview_difference(point,surface.origin);
    const double axial=edge_preview_dot(delta,surface.axis);
    auto normal=surface.axis;
    double distance=axial;
    if(surface.kind==Kind::Cylinder) {
        delta=edge_preview_offset(delta,surface.axis,-axial);
        const double radial=edge_preview_length(delta);
        normal=radial>1.e-12?edge_preview_offset({},delta,1./radial):surface.radial;
        distance=radial-surface.radius;
    }
    if(surface.kind==Kind::Torus) {
        const auto radial=edge_preview_normalized(edge_preview_offset(delta,surface.axis,-axial));
        const auto major=edge_preview_offset(surface.origin,radial?*radial:surface.radial,surface.radius);
        const auto minor=edge_preview_difference(point,major);
        const double length=edge_preview_length(minor);
        normal=length>1.e-12?edge_preview_offset({},minor,1./length):surface.axis;
        distance=length-surface.minor_radius;
    }
    if(surface.reversed){normal=edge_preview_offset({},normal,-1.);distance=-distance;}
    return {normal,distance};
}

std::optional<zima::kernel::Vec3> rolling_preview_center(
        const RollingPreviewMember& member,const zima::kernel::Vec3& point,
        zima::kernel::Vec3 guess) {
    using Kind=zima::kernel::SurfaceGeometry::Kind;
    // A plane cutting a cylinder has an affine correspondence between the
    // source ellipse and the offset ellipse, including its limiting point.
    for(std::size_t i=0;i<2;++i) {
        const auto& cylinder=*member.supports[i];
        const auto& plane=*member.supports[1-i];
        if(cylinder.kind!=Kind::Cylinder||plane.kind!=Kind::Plane)continue;
        const double divisor=edge_preview_dot(cylinder.axis,plane.axis);
        if(std::abs(divisor)<1.e-9)continue;
        auto radial=edge_preview_difference(point,cylinder.origin);
        radial=edge_preview_offset(radial,cylinder.axis,-edge_preview_dot(radial,cylinder.axis));
        const auto unit=edge_preview_normalized(radial);if(!unit)return {};
        const double radius=cylinder.radius+(cylinder.reversed?-1.:1.)*member.signed_radius;
        if(radius<-1.e-8)return {};
        auto center=edge_preview_offset(cylinder.origin,*unit,std::max(0.,radius));
        const double offset=(plane.reversed?-1.:1.)*member.signed_radius;
        const double shift=(offset-edge_preview_dot(edge_preview_difference(center,plane.origin),plane.axis))/divisor;
        return edge_preview_offset(center,cylinder.axis,shift);
    }
    // Parallel torus/cylinder supports admit an exact circle intersection
    // at each minor-angle station. This remains regular at a tangent runout,
    // where the general Newton system loses rank.
    for(std::size_t i=0;i<2;++i) {
        const auto& torus=*member.supports[i];
        const auto& cylinder=*member.supports[1-i];
        if(torus.kind!=Kind::Torus||cylinder.kind!=Kind::Cylinder||
           std::abs(std::abs(edge_preview_dot(torus.axis,cylinder.axis))-1.)>1.e-9)continue;
        auto baseline=edge_preview_difference(cylinder.origin,torus.origin);
        baseline=edge_preview_offset(baseline,torus.axis,-edge_preview_dot(baseline,torus.axis));
        const double d=edge_preview_length(baseline);if(d<1.e-10)return {};
        const auto x=edge_preview_offset({},baseline,1./d);
        const auto y=zima::kernel::dimension_cross(torus.axis,x);
        const double minor=torus.minor_radius+(torus.reversed?-1.:1.)*member.signed_radius;
        const double radius=cylinder.radius+(cylinder.reversed?-1.:1.)*member.signed_radius;
        if(minor<=0.||radius<=0.)return {};
        const auto delta=edge_preview_difference(point,torus.origin);
        const double height=edge_preview_dot(delta,torus.axis);
        const double rho=edge_preview_length(edge_preview_offset(delta,torus.axis,-height));
        double angle=std::atan2(height,rho-torus.radius);
        double gap=torus.radius-minor+radius-d;
        if(std::abs(gap)<64.*std::numeric_limits<double>::epsilon()*(d+torus.radius+minor+radius)) {
            gap=0.;
            if(std::abs(std::abs(angle)-std::numbers::pi)<1.e-5)
                angle=std::copysign(std::numbers::pi,angle);
        }
        const double r=torus.radius+minor*std::cos(angle);
        const double along=(r*r-radius*radius+d*d)/(2.*d);
        const double half=std::cos(angle*.5);
        const double h2=(gap+2.*minor*half*half)*(r+radius+d)*(d-r+radius)*(d+r-radius)/(4.*d*d);
        if(h2<-1.e-8)return {};
        const double branch=edge_preview_dot(delta,y)<0.?-1.:1.;
        auto center=edge_preview_offset(torus.origin,x,along);
        center=edge_preview_offset(center,y,branch*std::sqrt(std::max(0.,h2)));
        return edge_preview_offset(center,torus.axis,minor*std::sin(angle));
    }
    const auto station=guess;
    for(int iteration=0;iteration<32;++iteration) {
        const auto [a,da]=rolling_preview_normal_distance(*member.supports[0],guess);
        const auto [b,db]=rolling_preview_normal_distance(*member.supports[1],guess);
        const double fa=da-member.signed_radius,fb=db-member.signed_radius;
        if(std::max(std::abs(fa),std::abs(fb))<1.e-9)return guess;
        const auto tangent=edge_preview_normalized(zima::kernel::dimension_cross(a,b));
        if(!tangent)return {};
        const double fc=edge_preview_dot(edge_preview_difference(guess,station),*tangent);
        const auto bc=zima::kernel::dimension_cross(b,*tangent);
        const auto ca=zima::kernel::dimension_cross(*tangent,a);
        const auto ab=zima::kernel::dimension_cross(a,b);
        const double determinant=edge_preview_dot(a,bc);
        if(std::abs(determinant)<1.e-12)return {};
        guess=edge_preview_offset(guess,bc,-fa/determinant);
        guess=edge_preview_offset(guess,ca,-fb/determinant);
        guess=edge_preview_offset(guess,ab,-fc/determinant);
    }
    return {};
}

std::optional<zima::kernel::Vec3> rolling_preview_joint(
        const RollingPreviewMember& previous,const RollingPreviewMember& next,
        const zima::kernel::Vec3& point,const zima::kernel::Vec3& guess) {
    using Kind=zima::kernel::SurfaceGeometry::Kind;
    const auto same=[](const auto& a,const auto& b) {
        if(a.kind!=b.kind||a.reversed!=b.reversed)return false;
        if(std::abs(std::abs(edge_preview_dot(a.axis,b.axis))-1.)>1.e-9)return false;
        const auto delta=edge_preview_difference(a.origin,b.origin);
        if(a.kind==Kind::Torus)
            return std::abs(a.radius-b.radius)<1.e-7&&std::abs(a.minor_radius-b.minor_radius)<1.e-7&&
                edge_preview_length(delta)<1.e-7;
        return a.kind==Kind::Plane?std::abs(edge_preview_dot(delta,a.axis))<1.e-7:
            std::abs(a.radius-b.radius)<1.e-7&&edge_preview_length(
                edge_preview_offset(delta,a.axis,-edge_preview_dot(delta,a.axis)))<1.e-7;
    };
    for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<2;++j) {
        if(!same(*previous.supports[i],*next.supports[j]))continue;
        const auto& changed=previous.supports[1-i]->kind==Kind::Cylinder?
            *previous.supports[1-i]:*next.supports[1-j];
        if(changed.kind!=Kind::Cylinder)continue;
        const auto n=rolling_preview_normal_distance(changed,point).first;
        const auto origin=edge_preview_offset(point,n,previous.signed_radius);
        const auto& common=*previous.supports[i];
        if(common.kind==Kind::Plane) {
            const auto [normal,distance]=rolling_preview_normal_distance(common,origin);
            const double divisor=edge_preview_dot(normal,changed.axis);
            if(std::abs(divisor)>1.e-10)return edge_preview_offset(origin,changed.axis,
                (previous.signed_radius-distance)/divisor);
        } else {
            auto delta=edge_preview_difference(origin,common.origin);
            delta=edge_preview_offset(delta,common.axis,-edge_preview_dot(delta,common.axis));
            const auto direction=edge_preview_offset(changed.axis,common.axis,-edge_preview_dot(changed.axis,common.axis));
            const double radius=common.radius+(common.reversed?-1.:1.)*previous.signed_radius;
            const double a=edge_preview_dot(direction,direction),b=edge_preview_dot(delta,direction);
            const double discriminant=b*b-a*(edge_preview_dot(delta,delta)-radius*radius);
            if(a>1.e-12&&discriminant>=-1.e-10) {
                const auto first=edge_preview_offset(origin,changed.axis,(-b-std::sqrt(std::max(0.,discriminant)))/a);
                const auto last=edge_preview_offset(origin,changed.axis,(-b+std::sqrt(std::max(0.,discriminant)))/a);
                return edge_preview_length(edge_preview_difference(first,guess))<
                    edge_preview_length(edge_preview_difference(last,guess))?first:last;
            }
        }
    }
    // Both supports may change at the same smooth seam (two existing rounds).
    for(const auto candidate:{previous.last_center,next.first_center}) {
        bool on_all=true;
        for(const auto* member:{&previous,&next})for(const auto& support:member->supports)
            on_all=on_all&&std::abs(rolling_preview_normal_distance(*support,candidate).second-member->signed_radius)<1.e-7;
        if(on_all)return candidate;
    }
    return {};
}

std::vector<RollingPreviewMember> rolling_preview_path(
        const EdgeTreatmentPreviewPath& path,const zima::kernel::ViewerMesh& mesh,
        const zima::document::EdgeTreatmentParameters& parameters) {
    using Kind=zima::kernel::SurfaceGeometry::Kind;
    std::vector<RollingPreviewMember> result;
    bool curved=false;
    for(const auto& edge:path.edges) {
        if(edge.points.size()<2||edge.edge_treatment_side_references.size()!=2)return {};
        RollingPreviewMember member;
        for(std::size_t i=0;i<2;++i) {
            member.supports[i]=rolling_preview_support(mesh,edge.edge_treatment_side_references[i]);
            if(!member.supports[i]||(member.supports[i]->kind!=Kind::Plane&&member.supports[i]->kind!=Kind::Cylinder&&member.supports[i]->kind!=Kind::Torus)) {
                return {};
            }
            curved=curved||member.supports[i]->kind==Kind::Cylinder;
        }
        for(std::size_t index=0;index<edge.points.size();++index) {
            const auto section=edge_treatment_section(edge.points[index],edge.edge_treatment_side_directions[0][index],
                edge.edge_treatment_side_directions[1][index],parameters,true,parameters.primary_size);
            if(section&&section->radius_center) {
                const auto normal=rolling_preview_normal_distance(*member.supports[0],edge.points[index]).first;
                member.signed_radius=std::copysign(parameters.primary_size,
                    edge_preview_dot(edge_preview_difference(*section->radius_center,edge.points[index]),normal));
                break;
            }
        }
        if(member.signed_radius==0.)return {};
        for(const auto index:{std::size_t{},edge.points.size()-1}) {
            const auto a=rolling_preview_normal_distance(*member.supports[0],edge.points[index]).first;
            const auto b=rolling_preview_normal_distance(*member.supports[1],edge.points[index]).first;
            const double divisor=1.+edge_preview_dot(a,b);if(divisor<1.e-12)return {};
            auto guess=edge_preview_offset(edge.points[index],a,member.signed_radius/divisor);
            guess=edge_preview_offset(guess,b,member.signed_radius/divisor);
            const auto center=rolling_preview_center(member,edge.points[index],guess);
            if(!center)return {};
            (index==0?member.first_center:member.last_center)=*center;
        }
        const auto middle=rolling_preview_center(member,edge.points[edge.points.size()/2],
            edge_preview_offset(member.first_center,edge_preview_difference(member.last_center,member.first_center),.5));
        if(!middle)return {};
        member.middle_center=*middle;
        result.push_back(member);
    }
    if(!curved)return {};
    for(std::size_t i=1;i<result.size();++i) {
        const auto joint=rolling_preview_joint(result[i-1],result[i],path.edges[i].points.front(),result[i-1].last_center);
        if(!joint)return {};
        result[i-1].last_center=result[i].first_center=*joint;
        for(const auto* member:{&result[i-1],&result[i]})
            if(std::ranges::any_of(member->supports,[](const auto& support){return support->kind==Kind::Torus;}))
                result[i-1].trimmed_by_torus=result[i].trimmed_by_torus=true;
    }
    if(edge_preview_same_point(path.edges.front().points.front(),path.edges.back().points.back())) {
        const auto joint=rolling_preview_joint(result.back(),result.front(),path.edges.front().points.front(),result.back().last_center);
        if(!joint)return {};
        result.back().last_center=result.front().first_center=*joint;
    }
    return result;
}

std::optional<EdgeTreatmentSection> rolling_preview_section(
        const RollingPreviewMember& member,const zima::kernel::ViewerEdge& edge,
        std::size_t index,const zima::document::EdgeTreatmentParameters& parameters) {
    const double radius=parameters.primary_size;
    auto base=edge_treatment_section(edge.points[index],edge.edge_treatment_side_directions[0][index],
        edge.edge_treatment_side_directions[1][index],parameters,true,radius);
    if(!base) {base=EdgeTreatmentSection{};base->wire.overlay=true;}
    const auto na=rolling_preview_normal_distance(*member.supports[0],edge.points[index]).first;
    const auto nb=rolling_preview_normal_distance(*member.supports[1],edge.points[index]).first;
    const double divisor=1.+edge_preview_dot(na,nb);if(divisor<1.e-12)return {};
    auto guess=edge_preview_offset(edge.points[index],na,member.signed_radius/divisor);
    guess=edge_preview_offset(guess,nb,member.signed_radius/divisor);
    auto center=rolling_preview_center(member,edge.points[index],guess);
    if(!center)return {};
    if(index==0)center=member.first_center;
    else if(index+1==edge.points.size())center=member.last_center;
    else if(member.trimmed_by_torus||
            (member.supports[0]->kind==zima::kernel::SurfaceGeometry::Kind::Cylinder&&
             member.supports[1]->kind==zima::kernel::SurfaceGeometry::Kind::Cylinder)) {
        // The original support intersection and the rolling center curve have
        // different station ranges at a seam. A nearest station can overshoot
        // the contact endpoint; trim it to the same support transition as the
        // native construction, rather than drawing a short reversed rail.
        for(const auto& support:member.supports) {
            if(support->kind!=zima::kernel::SurfaceGeometry::Kind::Cylinder)continue;
            const auto radial=[&](const auto& p) {
                auto d=edge_preview_difference(p,support->origin);
                return edge_preview_normalized(edge_preview_offset(d,support->axis,-edge_preview_dot(d,support->axis)));
            };
            const auto first=radial(member.first_center),last=radial(member.last_center),middle=radial(member.middle_center),current=radial(*center);
            if(!first||!last||!middle||!current)continue;
            const auto angle=[&](const auto& direction) {
                return std::atan2(edge_preview_dot(zima::kernel::dimension_cross(*first,direction),support->axis),edge_preview_dot(*first,direction));
            };
            double end=angle(*last);if(std::abs(end)<1.e-8)continue;
            const double mid=angle(*middle);
            if(mid<std::min(0.,end)-1.e-7||mid>std::max(0.,end)+1.e-7)
                end+=end<0.?2.*std::numbers::pi:-2.*std::numbers::pi;
            double position=angle(*current);
            position+=2.*std::numbers::pi*std::round((.5*end-position)/(2.*std::numbers::pi));
            if(position<std::min(0.,end)-1.e-9||position>std::max(0.,end)+1.e-9)
                center=std::abs(position)<std::abs(position-end)?member.first_center:member.last_center;
        }
    }
    const auto contact_normal=[&](std::size_t side) {
        const auto& support=*member.supports[side];
        const bool collapsed=support.kind==zima::kernel::SurfaceGeometry::Kind::Cylinder&&
            std::abs(support.radius+(support.reversed?-1.:1.)*member.signed_radius)<1.e-8;
        return rolling_preview_normal_distance(support,collapsed?edge.points[index]:*center).first;
    };
    const auto a=contact_normal(0),b=contact_normal(1);
    base->first_tangent=edge_preview_offset(*center,a,-member.signed_radius);
    base->second_tangent=edge_preview_offset(*center,b,-member.signed_radius);
    base->radius_center=*center;base->curved_support=true;base->wire.points.clear();
    for(int sample=0;sample<=12;++sample) {
        const auto direction=edge_preview_blend_direction(a,b,sample/12.);
        base->wire.points.push_back(edge_preview_offset(*center,direction,-member.signed_radius));
    }
    base->plane_normal=zima::kernel::dimension_unit(zima::kernel::dimension_cross(a,b));
    base->radius_rim=base->wire.points[6];
    return base;
}
