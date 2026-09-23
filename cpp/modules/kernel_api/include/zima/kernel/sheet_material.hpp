#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <numbers>
#include <ranges>
#include <set>

namespace zima::kernel::sheet_material {
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 mul(Vec3 a,double k){return {a.x*k,a.y*k,a.z*k};}
inline Vec3 sub(Vec3 a,Vec3 b){return add(a,mul(b,-1));}
inline double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 unit(Vec3 a){const double n=std::sqrt(dot(a,a));if(n<1e-12)throw std::invalid_argument("Degenerate sheet material frame.");return mul(a,1/n);}

// A manufactured twist has clamped, untwisted ends.  The middle keeps a
// constant twist rate while equal fifth-order transition ramps bring that
// rate smoothly from/to zero.  This is the authored material law used by
// body creation, preview and both sheet-state directions.
inline constexpr double twist_transition_fraction=0.2;
inline double twist_progress(double t) {
    if(t<=0)return 0;if(t>=1)return 1;
    constexpr double a=twist_transition_fraction;
    constexpr double scale=1.0/(1.0-a);
    const auto integral=[](double u){return u*u*u-.5*u*u*u*u;};
    if(t<a)return a*integral(t/a)*scale;
    if(t>1-a)return 1-a*integral((1-t)/a)*scale;
    return (t-a*.5)*scale;
}
inline double twist_progress_derivative(double t) {
    if(t<=0||t>=1)return 0;
    constexpr double a=twist_transition_fraction;
    constexpr double scale=1.0/(1.0-a);
    const auto ramp=[](double u){return u*u*(3-2*u);};
    if(t<a)return ramp(t/a)*scale;
    if(t>1-a)return ramp((1-t)/a)*scale;
    return scale;
}

struct Coordinate {double along{},length{},depth{};bool continuation{};};
inline double positive_angle(double y,double x) {
    const double angle=std::atan2(y,x);
    return angle<-1e-8?angle+2*std::numbers::pi:angle;
}
inline Vec3 cone_generator(const SheetMaterialDefinition& f) {
    return add(mul(f.along,std::cos(f.cone_half_angle)),mul(f.radial,std::sin(f.cone_half_angle)));
}
inline Vec3 cone_normal(const SheetMaterialDefinition& f) {
    return sub(mul(f.radial,std::cos(f.cone_half_angle)),mul(f.along,std::sin(f.cone_half_angle)));
}
inline Coordinate coordinates(const SheetMaterialDefinition& frame,Vec3 point,
        bool continuation=false) {
    const auto delta=sub(point,frame.origin);
    Coordinate value{dot(delta,frame.along),dot(delta,frame.tangent),dot(delta,frame.radial),continuation};
    if(frame.kind==SheetMaterialDefinition::Kind::Twist) {
        if(frame.unfolded)return value;
        const double station=dot(delta,frame.tangent);
        const double angle=frame.signed_twist_angle*
            twist_progress(station/frame.formed_length);
        const double s=std::sin(angle),c=std::cos(angle);
        const auto across=add(mul(frame.along,c),mul(frame.radial,s));
        const auto depth=add(mul(frame.along,-s),mul(frame.radial,c));
        return {dot(delta,across),station*frame.developed_length/frame.formed_length,
            dot(delta,depth),false};
    }
    if(frame.kind==SheetMaterialDefinition::Kind::Cone) {
        const double s=std::sin(frame.cone_half_angle),c=std::cos(frame.cone_half_angle);
        if(frame.unfolded) {
            const double slant=frame.neutral_radius/s;
            const double x=slant+dot(delta,cone_generator(frame)),y=dot(delta,frame.tangent);
            return {std::hypot(x,y)-slant,positive_angle(y,x)/s*frame.neutral_radius,
                dot(delta,cone_normal(frame)),false};
        }
        const double radial=std::hypot(frame.radius+value.depth,value.length)-frame.radius;
        return {value.along*c+radial*s,positive_angle(value.length,frame.radius+value.depth)*frame.neutral_radius,
            radial*c-value.along*s,false};
    }
    if(frame.kind==SheetMaterialDefinition::Kind::Plane||frame.unfolded)return value;
    if(continuation) {
        const double s=std::sin(frame.angle),c=std::cos(frame.angle);
        const double y=value.length-frame.radius*s,z=value.depth-frame.radius*(c-1);
        value.length=frame.angle*frame.neutral_radius+y*c-z*s;
        value.depth=y*s+z*c;
    } else {
        const double radial=frame.radius+value.depth,y=value.length;
        double angle=std::atan2(y,radial);
        if(angle<-1e-8)angle+=2*std::numbers::pi;
        value.length=angle*frame.neutral_radius;
        value.depth=std::hypot(y,radial)-frame.radius;
    }
    return value;
}
inline Vec3 point(const SheetMaterialDefinition& frame,const Coordinate& value) {
    if(frame.kind==SheetMaterialDefinition::Kind::Twist) {
        if(frame.unfolded)
            return add(frame.origin,add(mul(frame.along,value.along),
                add(mul(frame.tangent,value.length),mul(frame.radial,value.depth))));
        const double station=value.length*frame.formed_length/frame.developed_length;
        const double angle=frame.signed_twist_angle*
            twist_progress(station/frame.formed_length);
        const double s=std::sin(angle),c=std::cos(angle);
        const auto across=add(mul(frame.along,c),mul(frame.radial,s));
        const auto depth=add(mul(frame.along,-s),mul(frame.radial,c));
        return add(frame.origin,add(mul(frame.tangent,station),
            add(mul(across,value.along),mul(depth,value.depth))));
    }
    if(frame.kind==SheetMaterialDefinition::Kind::Cone) {
        const double s=std::sin(frame.cone_half_angle),c=std::cos(frame.cone_half_angle);
        const double angle=value.length/frame.neutral_radius;
        if(frame.unfolded) {
            const double slant=frame.neutral_radius/s,station=slant+value.along,sector=angle*s;
            return add(frame.origin,add(mul(cone_generator(frame),station*std::cos(sector)-slant),
                add(mul(frame.tangent,station*std::sin(sector)),mul(cone_normal(frame),value.depth))));
        }
        const double radius=frame.radius+value.along*s+value.depth*c;
        return add(frame.origin,add(mul(frame.along,value.along*c-value.depth*s),
            add(mul(frame.radial,radius*std::cos(angle)-frame.radius),mul(frame.tangent,radius*std::sin(angle)))));
    }
    double y=value.length,z=value.depth;
    if(frame.kind==SheetMaterialDefinition::Kind::Cylinder&&!frame.unfolded) {
        const double angle=value.continuation?frame.angle:value.length/frame.neutral_radius;
        const double s=std::sin(angle),c=std::cos(angle),radius=frame.radius+value.depth;
        y=radius*s;z=radius*c-frame.radius;
        if(value.continuation){const double length=value.length-frame.angle*frame.neutral_radius;y+=length*c;z-=length*s;}
    }
    return add(frame.origin,add(mul(frame.along,value.along),add(mul(frame.tangent,y),mul(frame.radial,z))));
}
inline std::array<Vec3,3> basis(const SheetMaterialDefinition& frame,const Coordinate& coordinate) {
    if(frame.kind==SheetMaterialDefinition::Kind::Twist) {
        if(frame.unfolded)return {frame.along,frame.tangent,frame.radial};
        const double station=coordinate.length*frame.formed_length/frame.developed_length;
        const double angle=frame.signed_twist_angle*
            twist_progress(station/frame.formed_length);
        const double s=std::sin(angle),c=std::cos(angle);
        return {add(mul(frame.along,c),mul(frame.radial,s)),frame.tangent,
            add(mul(frame.along,-s),mul(frame.radial,c))};
    }
    if(frame.kind==SheetMaterialDefinition::Kind::Cone) {
        const double angle=coordinate.length/frame.neutral_radius;
        if(frame.unfolded) {
            const double sector=angle*std::sin(frame.cone_half_angle),s=std::sin(sector),c=std::cos(sector);
            const auto generator=cone_generator(frame);
            return {add(mul(generator,c),mul(frame.tangent,s)),add(mul(generator,-s),mul(frame.tangent,c)),cone_normal(frame)};
        }
        const auto radial=add(mul(frame.radial,std::cos(angle)),mul(frame.tangent,std::sin(angle)));
        const auto tangent=add(mul(frame.radial,-std::sin(angle)),mul(frame.tangent,std::cos(angle)));
        const double s=std::sin(frame.cone_half_angle),c=std::cos(frame.cone_half_angle);
        return {add(mul(frame.along,c),mul(radial,s)),tangent,sub(mul(radial,c),mul(frame.along,s))};
    }
    double angle=0;
    if(frame.kind==SheetMaterialDefinition::Kind::Cylinder&&!frame.unfolded)
        angle=coordinate.continuation?frame.angle:coordinate.length/frame.neutral_radius;
    const double s=std::sin(angle),c=std::cos(angle);
    return {frame.along,add(mul(frame.tangent,c),mul(frame.radial,-s)),
        add(mul(frame.tangent,s),mul(frame.radial,c))};
}
inline bool on_continuation(const SheetMaterialDefinition& frame,Vec3 p) {
    if(frame.continuation<=0)return false;
    if(frame.unfolded)return dot(sub(p,frame.origin),frame.tangent)>=frame.angle*frame.neutral_radius-1e-7;
    const Coordinate end{0,frame.angle*frame.neutral_radius,0,true};
    const auto delta=sub(p,point(frame,end));const auto directions=basis(frame,end);
    // At 180 degrees both caps lie in the same plane. The start cap must
    // never be mistaken for a station on the straight end continuation.
    const double depth=dot(delta,directions[2]);
    return dot(delta,directions[1])>=-1e-7&&std::abs(depth)<=frame.thickness+1e-6;
}
struct Transition {
    SheetMaterialDefinition before,after;
    Vec3 rigid_vector(Vec3 p)const{return add(mul(after.along,dot(p,before.along)),add(mul(after.tangent,dot(p,before.tangent)),mul(after.radial,dot(p,before.radial))));}
    Vec3 map(Vec3 p,bool continuation=false)const {
        if(before.unfolded==after.unfolded)return add(after.origin,rigid_vector(sub(p,before.origin)));
        return point(after,coordinates(before,p,continuation));
    }
    void move_frame(SheetMaterialDefinition& frame)const {
        // The child is rigid. Only its attachment station follows the parent
        // material map; its own material coordinates remain independent.
        if(before.unfolded==after.unfolded) {
            frame.origin=map(frame.origin);frame.along=rigid_vector(frame.along);
            frame.tangent=rigid_vector(frame.tangent);frame.radial=rigid_vector(frame.radial);return;
        }
        const auto c=coordinates(before,frame.origin,on_continuation(before,frame.origin));
        const auto a=basis(before,c),b=basis(after,c);
        const auto vector=[&](Vec3 v){return add(mul(b[0],dot(v,a[0])),add(mul(b[1],dot(v,a[1])),mul(b[2],dot(v,a[2]))));};
        frame.origin=point(after,c);frame.along=vector(frame.along);
        frame.tangent=vector(frame.tangent);frame.radial=vector(frame.radial);
    }
};
inline bool eligible(const SheetMaterialDefinition& region,bool unfold) {
    return region.kind!=SheetMaterialDefinition::Kind::Plane&&region.angle>1e-9&&region.unfolded!=unfold;
}
struct History {
    std::vector<SheetMaterialDefinition> regions;
    std::vector<SheetMaterialDefinition> sources;
    std::vector<std::optional<SheetMaterialDefinition>> attachment_sources;
};
inline std::vector<Transition> change(History& history,
        const SheetStateRequest& request) {
    auto& regions=history.regions;
    if(!std::isfinite(request.tolerance)||request.tolerance<1e-6||request.tolerance>1)
        throw std::invalid_argument("Invalid sheet state calculation tolerance.");
    std::set<std::string> selected;
    if(request.all){for(const auto& region:regions)if(eligible(region,request.unfold))selected.insert(region.owner_id);}
    else for(const auto& id:request.owners) {
        bool expanded=false;
        for(const auto& region:regions)if(region.feature_owner_id==id&&eligible(region,request.unfold)) {
            if(!selected.insert(region.owner_id).second)throw std::invalid_argument("Sheet state selection contains a duplicate feature.");expanded=true;
        }
        if(!expanded&&!selected.insert(id).second)throw std::invalid_argument("Sheet state selection contains a duplicate feature.");
    }
    if(selected.empty())throw std::invalid_argument("Select at least one sheet region to change its state.");
    for(const auto& id:selected) {
        const auto found=std::ranges::find(regions,id,&SheetMaterialDefinition::owner_id);
        if(found==regions.end()||!eligible(*found,request.unfold))
            throw std::invalid_argument("Selected sheet region is missing or already in the requested state.");
        if(found->kind==SheetMaterialDefinition::Kind::Twist) {
            if(found->formed_length<=1e-7||found->developed_length<=1e-7)
                throw std::invalid_argument("Twisted Sheet unfolding requires positive formed and developed lengths.");
            continue;
        }
        if(found->kind==SheetMaterialDefinition::Kind::Cone&&
            (found->cone_half_angle<=1e-9||found->cone_half_angle>=std::numbers::pi/2-1e-9))
            throw std::invalid_argument("Sheet unfolding requires a nondegenerate cone angle.");
        if(found->neutral_radius<=1e-7||found->angle>=2*std::numbers::pi-1e-7)
            throw std::invalid_argument("Sheet unfolding requires a positive neutral radius and an open angular region.");
    }
    std::vector<Transition> transitions;
    for(std::size_t index=0;index<regions.size();++index) {
        auto& region=regions[index];
        // Always derive frames from their authored values. Returning to an
        // unchanged source-parent state copies the source origin and axes
        // exactly; it never inverts an earlier numerical frame transform.
        auto next=history.sources[index];
        next.unfolded=selected.contains(region.owner_id)?request.unfold:region.unfolded;
        if(const auto& source=history.attachment_sources[index];source) {
            const auto parent=std::ranges::find(regions,source->owner_id,&SheetMaterialDefinition::owner_id);
            if(parent!=regions.end()&&*parent!=*source)Transition{*source,*parent}.move_frame(next);
        }
        if(next!=region)transitions.push_back({region,next});
        region=std::move(next);
    }
    return transitions;
}
inline History regions_before(const std::vector<HistoryOperation>& operations,std::size_t limit) {
    History history;
    for(std::size_t i=0;i<std::min(limit,operations.size());++i) {
        const auto& operation=operations[i];if(operation.suppressed||!operation.input_error.empty())continue;
        if(operation.sheet_material) {
            const auto& material=*operation.sheet_material;
            const auto parent=std::ranges::find(history.regions,material.parent_owner_id,&SheetMaterialDefinition::owner_id);
            history.attachment_sources.push_back(parent==history.regions.end()?std::nullopt:std::optional{*parent});
            history.sources.push_back(material);history.regions.push_back(material);
        }
        for(const auto& material:operation.sheet_regions) {
            const auto parent=std::ranges::find(history.regions,material.parent_owner_id,&SheetMaterialDefinition::owner_id);
            history.attachment_sources.push_back(parent==history.regions.end()?std::nullopt:std::optional{*parent});
            history.sources.push_back(material);history.regions.push_back(material);
        }
        if(const auto* state=std::get_if<SheetStateRequest>(&operation.primitive))static_cast<void>(change(history,*state));
    }
    return history;
}
inline bool is_bend_line(const AxisReference& reference) {
    return reference.semantic_key.starts_with("sheet-bend-line:from:");
}
// A development centerline is halfway through the angular material span,
// on the inner skin. Cones use the generator of the annular-sector midpoint.
inline std::vector<ViewerAxis> bend_lines(const std::vector<HistoryOperation>& operations,
        std::size_t limit,const History& history,const std::string& owner) {
    std::vector<ViewerAxis> result;
    for(const auto& region:history.regions) {
        if(!region.unfolded||region.kind==SheetMaterialDefinition::Kind::Plane||
            region.kind==SheetMaterialDefinition::Kind::Twist)continue;
        const auto source=std::ranges::find_if(operations.begin(),operations.begin()+std::min(limit,operations.size()),
            [&](const auto& op){return (op.owner_id==region.owner_id&&op.sheet_material)||
                (op.owner_id==region.feature_owner_id&&std::ranges::any_of(op.sheet_regions,[&](const auto& value){return value.owner_id==region.owner_id;}));});
        if(source==operations.begin()+std::min(limit,operations.size()))
            throw std::runtime_error("Bend line source is missing.");
        const auto* source_material=source->sheet_material?&*source->sheet_material:nullptr;
        if(!source_material)source_material=&*std::ranges::find(source->sheet_regions,region.owner_id,&SheetMaterialDefinition::owner_id);
        if(const auto* revolve=std::get_if<RevolutionRequest>(&source->primitive)) {
            const auto* profile=std::get_if<ExtrusionRequest::CurvedProfile>(&revolve->outer_profile);
            const auto* generator=profile&&profile->curves.size()==1?std::get_if<ExtrusionRequest::LineCurve>(&profile->curves.front()):nullptr;
            if(!generator||revolve->outer_edge_source_ids.size()!=1)
                throw std::runtime_error("Revolved sheet bend line requires its authored generator.");
            const double first=coordinates(*source->sheet_material,generator->start).along;
            const double last=coordinates(*source->sheet_material,generator->end).along;
            const Coordinate middle{(first+last)/2,region.angle*region.neutral_radius/2,
                region.thickness_sign<0?-region.thickness:0};
            result.push_back({point(region,middle),basis(region,middle)[0],std::abs(last-first),
                {owner,"sheet-bend-line:from:"+region.owner_id+":"+revolve->outer_edge_source_ids.front(),{}},"Osa ohybu"});
            continue;
        }
        const auto* group=std::get_if<FeatureGroupRequest>(&source->primitive);
        const Sweep3DRequest* sweep=nullptr;
        if(group)for(const auto& child:group->children)if(const auto* candidate=std::get_if<Sweep3DRequest>(&child))
            if(std::ranges::any_of(candidate->path_segments,[&](const auto& segment){return segment.source_id==region.curved_source_id;}))sweep=candidate;
        if(!sweep||sweep->sections.size()<2)throw std::runtime_error("Bend line requires its authored start and end sections.");
        const double middle_length=source_material->angle*source_material->neutral_radius/2;
        const auto station_length=[&](const auto& section){return coordinates(*source_material,sweep->path_points.at(section.point_index)).length;};
        std::size_t upper=1;
        while(upper+1<sweep->sections.size()&&station_length(sweep->sections[upper])<middle_length)++upper;
        const double first_length=station_length(sweep->sections[upper-1]),last_length=station_length(sweep->sections[upper]);
        const double blend=last_length>first_length?std::clamp((middle_length-first_length)/(last_length-first_length),0.,1.):.5;
        double low=0,high=0;
        for(const auto index:{upper-1,upper}) {
            const auto& section=sweep->sections[index];const double weight=index==upper?blend:1-blend;
            const auto* polygon=std::get_if<ExtrusionRequest::PolygonProfile>(&section.profile.outer_profile);
            if(!polygon||polygon->vertices.empty())throw std::runtime_error("Bend line section is missing.");
            double a=INFINITY,b=-INFINITY;
            for(const auto& p:polygon->vertices) {
                const double u=dot(sub(p,source_material->origin),source_material->along);
                a=std::min(a,u);b=std::max(b,u);
            }
            low+=a*weight;high+=b*weight;
        }
        result.push_back({point(region,{(low+high)/2,region.angle*region.neutral_radius/2,region.thickness_sign<0?-region.thickness:0}),
            region.along,high-low,{owner,"sheet-bend-line:from:"+region.owner_id+":"+region.curved_source_id,{}},"Osa ohybu"});
    }
    return result;
}
} // namespace zima::kernel::sheet_material
