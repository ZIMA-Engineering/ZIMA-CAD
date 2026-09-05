#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <numbers>
namespace zima::app {
inline kernel::ViewerMesh shaft_thread_preview(const document::HistoryContainer& container,
        const kernel::ViewerReferenceGeometry& references) {
    const auto request=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(container,&references));
    kernel::ViewerMesh mesh;
    const auto& a=request.axis_direction;const auto& u=request.radial_direction;
    const kernel::Vec3 v{a.y*u.z-a.z*u.y,a.z*u.x-a.x*u.z,a.x*u.y-a.y*u.x};
    const auto point=[&](double z,double radius,double angle) {
        return kernel::Vec3{request.origin.x+a.x*z+radius*(u.x*std::cos(angle)+v.x*std::sin(angle)),
            request.origin.y+a.y*z+radius*(u.y*std::cos(angle)+v.y*std::sin(angle)),
            request.origin.z+a.z*z+radius*(u.z*std::cos(angle)+v.z*std::sin(angle))};
    };
    const double first=request.start_offset;
    const double last=first+request.length-request.runout_end;
    const auto end_at=[&](double angle) {
        if (!request.end_plane_origin) return last;
        const auto p=point(0,request.root_radius,angle);const auto& q=*request.end_plane_origin;const auto& n=request.end_plane_normal;
        return ((q.x-p.x)*n.x+(q.y-p.y)*n.y+(q.z-p.z)*n.z)/(a.x*n.x+a.y*n.y+a.z*n.z);
    };
    for (int end=0;end<2;++end) {
        kernel::ViewerEdge edge;
        edge.reference={container.id,end==0 ? "thread:boundary:root:start" : "thread:boundary:root:end",{}};
        for (int i=0;i<=64;++i) {
            const double angle=2*std::numbers::pi*i/64;
            edge.points.push_back(point(end ? end_at(angle) : first,request.root_radius,angle));
        }
        mesh.edges.push_back(std::move(edge));
    }
    for (int side=0;side<1;++side) {
        const double angle=std::numbers::pi*side;
        kernel::ViewerEdge edge;
        edge.reference={container.id,"thread:boundary:root:side:"+std::to_string(side),{}};
        edge.points={point(first,request.root_radius,angle),point(end_at(angle),request.root_radius,angle)};
        if (request.runout_end>0) edge.points.push_back(point(last+request.runout_end,request.nominal_radius,angle));
        mesh.edges.push_back(std::move(edge));
    }
    if (request.runout_end>0) {
        kernel::ViewerEdge edge;
        edge.reference={container.id,"thread:boundary:runout:end",{}};
        for (int i=0;i<=64;++i)
            edge.points.push_back(point(last+request.runout_end,request.nominal_radius,
                2*std::numbers::pi*i/64));
        mesh.edges.push_back(std::move(edge));
    }
    const auto center=point((first+last)*0.5,0,0);
    const auto rim=point((first+last)*0.5,request.root_radius,0);
    kernel::ViewerDimension diameter;
    diameter.reference={container.id,"parameter:root_diameter",{}};
    diameter.kind=kernel::ViewerDimensionKind::Diameter;
    diameter.value=container.shaft_thread.root_diameter;
    diameter.witness_first=center;diameter.witness_second=rim;
    diameter.line_first=point((first+last)*0.5,-request.root_radius,0);
    diameter.line_second=point((first+last)*0.5,request.root_radius+6,0);
    diameter.plane_normal=a;
    diameter.participant_semantic_keys={"thread:surface:root"};
    mesh.dimensions.push_back(diameter);
    kernel::ViewerDimension length;
    length.reference={container.id,container.shaft_thread.end_condition==document::EndCondition::Length
        ? "parameter:length" : "measurement:shaft_length",{}};
    length.value=last;length.witness_first=request.origin;length.witness_second=point(last,0,0);
    length.line_first=point(0,request.nominal_radius+6,0);length.line_second=point(last,request.nominal_radius+6,0);
    length.plane_normal=v;length.driving=container.shaft_thread.end_condition==document::EndCondition::Length;
    mesh.dimensions.push_back(length);
    return mesh;
}
}
