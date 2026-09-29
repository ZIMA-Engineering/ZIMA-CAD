#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
namespace zima::kernel {
// Presentation only: retain the real topology and its persisted references.
inline bool smooth_transition_junction(const ViewerEdge& edge) {
    const auto& refs=edge.edge_treatment_side_references;
    const auto& dirs=edge.edge_treatment_side_directions;
    if(refs.size()!=2||dirs.size()!=2||edge.points.size()<2||
       dirs[0].size()!=edge.points.size()||dirs[1].size()!=edge.points.size())return false;
    if(refs[0].sheet_role!=refs[1].sheet_role||
       (refs[0].sheet_role!=SheetFaceRole::SideA&&refs[0].sheet_role!=SheetFaceRole::SideB)||
       refs[0].owner_id!=refs[1].owner_id||refs[0].instance_path!=refs[1].instance_path)return false;
    const auto panel=[](const auto& r){return r.semantic_key.find("transition:authored-panel:")!=std::string::npos;};
    const auto bend=[](const auto& r){return r.semantic_key.find("transition:authored-bend:")!=std::string::npos;};
    if(refs[0].semantic_key==refs[1].semantic_key||
       !(panel(refs[0])||bend(refs[0]))||!(panel(refs[1])||bend(refs[1])))return false;
    for(std::size_t i=0;i<edge.points.size();++i) {
        const auto a=dirs[0][i],b=dirs[1][i];
        const double aa=a.x*a.x+a.y*a.y+a.z*a.z,bb=b.x*b.x+b.y*b.y+b.z*b.z;
        if(!(aa>1e-18&&bb>1e-18))return false;
        const double cosine=(a.x*b.x+a.y*b.y+a.z*b.z)/std::sqrt(aa*bb);
        if(!std::isfinite(cosine)||cosine>-1+1e-6)return false;
    }
    return true;
}
}
