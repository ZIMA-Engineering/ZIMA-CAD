#include <zima/document/feature_parameters.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <algorithm>
#include <cmath>

namespace zima::document {
bool FeatureUpToParameters::neutral() const {
    return length_correction==0&&angle_correction_degrees==0&&
        !std::signbit(length_correction)&&!std::signbit(angle_correction_degrees)&&!primary_cap_source&&retained_patches.empty();
}
std::optional<bool> FeatureUpToParameters::retained_side(const kernel::FaceReference& source,
        const kernel::FaceReference& adjacent) const {
    const auto found=std::ranges::find_if(retained_patches,[&](const auto& choice){return choice.source==source&&choice.adjacent==adjacent;});
    return found==retained_patches.end()?std::nullopt:std::optional<bool>{found->positive};
}
void FeatureUpToParameters::set_retained_side(const kernel::FaceReference& source,
        const kernel::FaceReference& adjacent,bool positive) {
    const auto found=std::ranges::find_if(retained_patches,[&](const auto& choice){return choice.source==source&&choice.adjacent==adjacent;});
    if(found==retained_patches.end())retained_patches.push_back({source,adjacent,positive});else found->positive=positive;
}
void prune_feature_up_to_patch_choices(FeatureSideParameters& side) {
    if(side.targets.size()<2)side.up_to.primary_cap_source.reset();
    const auto index=[&](const kernel::FaceReference& reference) {
        const auto found=std::ranges::find_if(side.targets,[&](const auto& target){return target.reference==reference;});
        return static_cast<std::size_t>(found-side.targets.begin());
    };
    std::erase_if(side.up_to.retained_patches,[&](const auto& choice){
        const auto source=index(choice.source),adjacent=index(choice.adjacent),count=side.targets.size();
        return source==count||adjacent==count||(source+1!=adjacent&&adjacent+1!=source);
    });
}
bool resolve_feature_up_to_choices(FeatureParameters& parameters,const std::string& container_id,
        const std::string& feature_id,const std::vector<kernel::ResolvedOrderedUpToLimit>& resolutions) {
    bool changed=false;
    for(const auto& resolved:resolutions)if(resolved.owner_id==container_id) {
        const auto parent=kernel::feature_side_parent(resolved.profile_region_id);
        if(!parent||parent->feature_id!=feature_id)throw std::invalid_argument("Invalid Feature definition.");
        const std::size_t index=parameters.symmetric?0:parent->side==kernel::FeatureSide::End?0:1;
        auto& side=parameters.sides[index];
        if(resolved.primary_cap_source) {
            if(side.up_to.primary_cap_source&&*side.up_to.primary_cap_source!=*resolved.primary_cap_source)
                throw std::invalid_argument("Invalid Feature definition.");
            if(!side.up_to.primary_cap_source){side.up_to.primary_cap_source=resolved.primary_cap_source;changed=true;}
        }
        for(const auto& choice:resolved.retained_patches) {
            const auto source=std::ranges::find_if(side.targets,[&](const auto& t){return t.reference==choice.source;});
            const auto adjacent=std::ranges::find_if(side.targets,[&](const auto& t){return t.reference==choice.adjacent;});
            if(source==side.targets.end()||adjacent==side.targets.end()||std::abs(source-adjacent)!=1)
                throw std::invalid_argument("Invalid Feature definition.");
            const auto existing=side.up_to.retained_side(choice.source,choice.adjacent);
            if(existing&&*existing!=choice.positive)throw std::invalid_argument("Invalid Feature definition.");
            if(!existing){side.up_to.set_retained_side(choice.source,choice.adjacent,choice.positive);changed=true;}
        }
    }
    return changed;
}
} // namespace zima::document
