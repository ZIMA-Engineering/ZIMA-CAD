#pragma once
#include "geometry_kernel.hpp"
#include <cmath>
#include <stdexcept>
namespace zima::kernel {
inline ThreadSurfaceRequest resolve_shaft_thread(ThreadSurfaceRequest value) {
    const auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto sub=[](Vec3 a,Vec3 b){return Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto add=[](Vec3 a,Vec3 b,double k){return Vec3{a.x+k*b.x,a.y+k*b.y,a.z+k*b.z};};
    if (!value.shaft_face || !value.shaft_face->surface ||
        value.shaft_face->surface->kind!=SurfaceGeometry::Kind::Cylinder ||
        value.shaft_face->surface->reversed ||
        !value.shaft_start.surface || value.shaft_start.surface->kind!=SurfaceGeometry::Kind::Plane)
        throw std::runtime_error("Vyberte válcovou plochu hřídele a počáteční rovinnou plochu.");
    const auto& cylinder=*value.shaft_face->surface;
    const auto& start=*value.shaft_start.surface;
    const double divisor=dot(cylinder.axis,start.axis);
    if (std::abs(divisor)<1e-8) throw std::runtime_error("Počáteční plocha neprotíná osu závitu.");
    const double start_parameter=dot(sub(start.origin,cylinder.origin),start.axis)/divisor;
    value.origin=add(cylinder.origin,cylinder.axis,start_parameter);
    const double sign=(cylinder.axial_min+cylinder.axial_max)*0.5>=start_parameter ? 1.0 : -1.0;
    value.axis_direction={sign*cylinder.axis.x,sign*cylinder.axis.y,sign*cylinder.axis.z};
    value.radial_direction=cylinder.radial;
    const double extent=std::max(sign*(cylinder.axial_min-start_parameter),sign*(cylinder.axial_max-start_parameter));
    double begin=std::max(0.0,std::min(sign*(cylinder.axial_min-start_parameter),sign*(cylinder.axial_max-start_parameter)));
    if (!(value.root_radius>0) || !std::isfinite(value.root_radius))
        throw std::runtime_error("Patní průměr musí být kladný.");
    const bool outside_shaft=value.root_radius>=cylinder.radius-1e-9;
    if (value.shaft_chamfer) {
        if (!value.shaft_chamfer->surface || value.shaft_chamfer->surface->kind!=SurfaceGeometry::Kind::Cone)
            throw std::runtime_error("Vstupní sražení musí být kuželová plocha.");
        const auto& cone=*value.shaft_chamfer->surface;
        if (std::abs(std::abs(dot(cone.axis,cylinder.axis))-1)>1e-7)
            throw std::runtime_error("Sražení není souosé s hřídelí.");
        const auto delta=sub(cone.origin,cylinder.origin);
        const auto radial=sub(delta,add({},cylinder.axis,dot(delta,cylinder.axis)));
        if (dot(radial,radial)>1e-10 || std::abs(std::tan(cone.semi_angle))<1e-9)
            throw std::runtime_error("Sražení není souosé s hřídelí.");
        const double axial=(value.root_radius-cone.radius)/std::tan(cone.semi_angle);
        if (outside_shaft) {
            begin=0; // Preserve an independently sized thread even outside material.
        } else if (axial<cone.axial_min-1e-6 || axial>cone.axial_max+1e-6) {
            const double minimum_radius=std::min(cone.radius+cone.axial_min*std::tan(cone.semi_angle),
                cone.radius+cone.axial_max*std::tan(cone.semi_angle));
            if (value.root_radius>minimum_radius+1e-7)
                throw std::runtime_error("Patní plocha neprotíná vybrané sražení.");
            begin=0; // Root lies wholly inside the small end of the chamfer.
        } else begin=std::max(0.0,dot(sub(add(cone.origin,cone.axis,axial),value.origin),value.axis_direction));
    }
    double end=value.shaft_through_all ? extent : value.length;
    double allowed_extent=extent;
    value.end_plane_origin.reset();
    if (value.shaft_end) {
        if (!value.shaft_end->surface)
            throw std::runtime_error("Chybí geometrie koncové plochy závitu.");
        const auto& target=*value.shaft_end->surface;
        if (target.kind==SurfaceGeometry::Kind::Plane) {
            const double denominator=dot(target.axis,value.axis_direction);
            if (std::abs(denominator)<1e-8) throw std::runtime_error("Koncová plocha neprotíná osu závitu.");
            end=dot(sub(target.origin,value.origin),target.axis)/denominator;
            value.end_plane_origin=target.origin;value.end_plane_normal=target.axis;
        } else {
            const double alignment=dot(target.axis,value.axis_direction);
            const auto delta=sub(target.origin,value.origin);
            const auto radial=sub(delta,add({},value.axis_direction,dot(delta,value.axis_direction)));
            if (std::abs(std::abs(alignment)-1)>1e-7 || dot(radial,radial)>1e-10)
                throw std::runtime_error("Koncový válec nebo sražení musí být souosé s hřídelí.");
            const auto station=[&](double axial) {
                return dot(sub(add(target.origin,target.axis,axial),value.origin),value.axis_direction);
            };
            if (target.kind==SurfaceGeometry::Kind::Cylinder) {
                end=std::max(station(target.axial_min),station(target.axial_max));
            } else if (target.kind==SurfaceGeometry::Kind::Cone) {
                const double slope=std::tan(target.semi_angle);
                if (!std::isfinite(slope) || slope*alignment>=-1e-9)
                    throw std::runtime_error("Vyberte sražení na konci hřídele ve směru závitu.");
                const double axial=(value.root_radius-target.radius)/slope;
                if (outside_shaft) {
                    end=std::max(station(target.axial_min),station(target.axial_max));
                } else if (axial<target.axial_min-1e-6 || axial>target.axial_max+1e-6) {
                    const double minimum_radius=std::min(target.radius+target.axial_min*slope,
                        target.radius+target.axial_max*slope);
                    if (value.root_radius>minimum_radius+1e-7)
                        throw std::runtime_error("Patní plocha závitu neprotíná vybrané koncové sražení.");
                    // A shallow chamfer remains entirely outside the root.
                    // The root stays in material up to the terminal end face.
                    end=std::max(station(target.axial_min),station(target.axial_max));
                } else end=station(axial);
                // The root extends into the chamfer beyond the end of the
                // nominal cylinder. Permit only an adjoining end chamfer.
                if (end>extent+1e-6) {
                    const double join=station((cylinder.radius-target.radius)/slope);
                    if (std::abs(join-extent)>1e-5)
                        throw std::runtime_error("Koncové sražení nenavazuje na vybraný válec hřídele.");
                    allowed_extent=end;
                }
            } else throw std::runtime_error("Pro Až k vyberte rovinu, válec nebo sražení.");
            value.end_plane_origin=add(value.origin,value.axis_direction,end);
            value.end_plane_normal=value.axis_direction;
        }
    }
    if (!std::isfinite(end) || end<=begin+1e-7 || end>allowed_extent+1e-6)
        throw std::runtime_error("Délka závitu se musí vejít do vybrané části hřídele.");
    const double runout=value.shaft_runout && !value.shaft_end && !value.shaft_through_all
        ? std::min(value.runout_end,std::max(0.0,extent-end)) : 0.0;
    value.nominal_radius=cylinder.radius;
    value.start_offset=begin;value.length=end+runout-begin;
    value.runout_start=0;value.runout_end=runout;
    value.side=ThreadSurfaceRequest::Side::External;
    return value;
}
}
