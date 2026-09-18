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
    else for(const auto& id:request.owners)if(!selected.insert(id).second)
        throw std::invalid_argument("Sheet state selection contains a duplicate feature.");
    if(selected.empty())throw std::invalid_argument("Select at least one sheet region to change its state.");
    for(const auto& id:selected) {
        const auto found=std::ranges::find(regions,id,&SheetMaterialDefinition::owner_id);
        if(found==regions.end()||!eligible(*found,request.unfold))
            throw std::invalid_argument("Selected sheet region is missing or already in the requested state.");
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
        if(const auto* state=std::get_if<SheetStateRequest>(&operation.primitive))static_cast<void>(change(history,*state));
    }
    return history;
}
} // namespace zima::kernel::sheet_material
