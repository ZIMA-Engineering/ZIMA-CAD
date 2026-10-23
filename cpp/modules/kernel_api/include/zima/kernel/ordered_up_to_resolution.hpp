#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>

namespace zima::kernel {
// Canonicalize accepted automatic intent only. No coordinate, side, tolerance
// or geometry is normalized; a conflicting explicit choice is an error.
inline bool resolve_ordered_up_to_operation(HistoryOperation& operation,
        const std::vector<ResolvedOrderedUpToLimit>& resolutions) {
    bool changed=false;
    const auto resolve=[&](auto& request) {
        using Request=std::decay_t<decltype(request)>;
        if constexpr(std::is_same_v<Request,ExtrusionRequest>||std::is_same_v<Request,RevolutionRequest>) {
            if(!request.ordered_end_limit)return;
            for(const auto& resolution:resolutions)if(resolution.owner_id==operation.owner_id&&
                    resolution.profile_region_id==request.profile_region_id) {
                auto& rows=request.ordered_end_limit->patches;
                if(resolution.primary_cap_source) {
                    auto& source=request.ordered_end_limit->primary_cap_source;
                    if(source&&*source!=*resolution.primary_cap_source)throw std::invalid_argument("Invalid ordered Feature Up To resolution");
                    if(!source){source=resolution.primary_cap_source;changed=true;}
                }
                const auto assign=[&](std::optional<bool>& side,const FaceReference& source,const FaceReference& adjacent) {
                    const auto choice=std::ranges::find_if(resolution.retained_patches,[&](const auto& value){return value.source==source&&value.adjacent==adjacent;});
                    if(choice==resolution.retained_patches.end()||(side&&*side!=choice->positive))
                        throw std::invalid_argument("Invalid ordered Feature Up To resolution");
                    if(!side){side=choice->positive;changed=true;}
                };
                for(std::size_t row=0;row<rows.size();++row) {
                    if(row)assign(rows[row].keep_previous_positive,rows[row].source_reference,rows[row-1].source_reference);
                    if(row+1<rows.size())assign(rows[row].keep_next_positive,rows[row].source_reference,rows[row+1].source_reference);
                }
            }
        }
    };
    std::visit([&](auto& request) {
        using Request=std::decay_t<decltype(request)>;
        if constexpr(std::is_same_v<Request,FeatureGroupRequest>)for(auto& child:request.children)std::visit(resolve,child);
        else resolve(request);
    },operation.primitive);
    return changed;
}
} // namespace zima::kernel
