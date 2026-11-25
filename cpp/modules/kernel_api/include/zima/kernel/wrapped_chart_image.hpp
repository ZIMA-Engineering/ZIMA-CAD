#pragma once
#include <zima/kernel/wrapped_image.hpp>
#include <zima/kernel/surface_wrap_chain.hpp>

namespace zima::kernel {
inline ViewerImage wrapped_chart_image(const ViewerImage& source,const WrappingRequest& request,
        const std::vector<WrapChart>& charts,const ViewerMesh& mesh,bool allow_empty=false) {
    ViewerImage result=source;result.surface_vertices.clear();result.surface_uv.clear();
    for(const auto& chart:charts) {
        ViewerMesh target;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i)
            if(std::ranges::any_of(chart.coverage,[&](const auto& face){return wrapped_image_face_matches(mesh.triangle_references[i],face);}))
                for(std::size_t corner=0;corner<3;++corner) {
                    target.triangles.push_back(static_cast<std::uint32_t>(target.vertices.size()));
                    target.vertices.push_back(mesh.vertices.at(mesh.triangles.at(i*3+corner)));
                }
        if(target.triangles.empty())continue;
        auto image=wrapped_image(source,request,chart.map,target,true,charts.size()>1);
        if(result.surface_vertices.empty())result.corners=image.corners;
        result.surface_vertices.insert(result.surface_vertices.end(),image.surface_vertices.begin(),image.surface_vertices.end());
        result.surface_uv.insert(result.surface_uv.end(),image.surface_uv.begin(),image.surface_uv.end());
    }
    if(result.surface_vertices.empty()&&!allow_empty)throw std::runtime_error("Wrapped geometry extends outside the selected face.");
    return result;
}
}
