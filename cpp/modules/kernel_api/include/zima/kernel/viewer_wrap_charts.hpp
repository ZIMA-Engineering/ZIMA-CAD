#pragma once
#include <zima/kernel/surface_wrap_chain.hpp>

namespace zima::kernel {
inline std::vector<WrapChart> viewer_wrap_charts(const ViewerMesh& mesh,const std::vector<FaceReference>& selected,
        Vec3 origin,Vec3 x,Vec3 y,bool reverse_u,bool reverse_v,double tolerance,
        const std::function<Vec3(Vec3)>& local,const std::function<bool(const FaceReference&)>& allowed={}) {
    using namespace wrap_detail;
    std::vector<WrapChart> charts;
    const auto faces=viewer_surface_references(mesh,allowed);
    for(const auto& reference:selected) {
        if(std::ranges::any_of(charts,[&](const auto& chart){return std::ranges::find(chart.coverage,reference)!=chart.coverage.end();}))continue;
        const auto face=std::ranges::find(faces,reference);
        if(face==faces.end()||!(face->surface?face->surface:face->contour_surface))throw std::runtime_error("Wrap target is unavailable.");
        auto surface=*(face->surface?face->surface:face->contour_surface);
        const auto old_origin=surface.origin;
        surface.origin=local(old_origin);surface.axis=sub(local(add(old_origin,surface.axis)),surface.origin);
        surface.radial=sub(local(add(old_origin,surface.radial)),surface.origin);
        auto coverage=continued_viewer_surfaces(mesh,{reference},tolerance,allowed);
        if(charts.empty()) {charts.push_back({surface,reference,std::move(coverage),SurfaceWrap(surface,origin,x,y,reverse_u,reverse_v),{}});continue;}
        auto& previous=charts.back();std::optional<WrapJunction> join;
        const auto in=[](const auto& faces,const auto& face){return std::ranges::find(faces,face)!=faces.end();};
        for(const auto& edge:mesh.edges) {
            const auto& sides=edge.edge_treatment_side_references;
            if(sides.size()!=2||edge.points.size()<2||
                !((in(previous.coverage,sides[0])&&in(coverage,sides[1]))||(in(previous.coverage,sides[1])&&in(coverage,sides[0]))))continue;
            const auto a=local(edge.points.front()),b=local(edge.points.back());
            if(length(sub(b,a))<=tolerance)continue;
            const auto direction=unit(sub(b,a));
            if(std::ranges::any_of(edge.points,[&](auto p){return length(cross(sub(local(p),a),direction))>tolerance;}))
                throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
            WrapJunction candidate{scale(add(a,b),.5),direction,{},"wrap:viewer-junction"};
            if(join) {
                if(length(cross(sub(candidate.point,join->point),join->direction))>tolerance||std::abs(dot(candidate.direction,join->direction))<1-1e-10)
                    throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
            }else join=std::move(candidate);
        }
        if(!join)throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
        // The nearest current display triangle supplies the side of the real
        // shared edge. It does not establish adjacency or replace native trims.
        double closest=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<mesh.triangle_references.size();++i)if(in(previous.coverage,mesh.triangle_references[i])) {
            const auto a=local(mesh.vertices.at(mesh.triangles.at(i*3))),b=local(mesh.vertices.at(mesh.triangles.at(i*3+1))),c=local(mesh.vertices.at(mesh.triangles.at(i*3+2)));
            const auto center=scale(add(add(a,b),c),1./3);
            const double distance=length(sub(center,join->point));
            if(distance<closest){closest=distance;join->previous_interior=center;}
        }
        charts.push_back(continue_wrap_chart(previous,surface,reference,std::move(coverage),*join,tolerance));
    }
    return charts;
}
}
