#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/profile_targets.hpp>
#include <algorithm>

namespace zima::workspace::detail {
// Borrow the exact current input snapshot. Target resolution has already
// validated ownership; attaching sources never evaluates Parts or Assembly cuts.
inline void attach_ordered_up_to_sources(const assembly::AssemblyDocument& document,
        std::vector<kernel::HistoryOperation>& operations) {
    const auto attach=[&](kernel::OrderedUpToPatch& patch) {
        if(document::profile_target_is_datum(patch.source_reference))return;
        const auto path=assembly::InstancePath::decode(patch.source_reference.instance_path);
        if(path.occurrence_ids.empty())throw std::runtime_error("The original extrusion target is unavailable.");
        const auto* root=document.find_occurrence(path.occurrence_ids.front());
        if(!root||root->suppressed)throw std::runtime_error("The original extrusion target is unavailable.");
        const auto* metadata=&root->nested_snapshot;
        for(std::size_t i=1;i<path.occurrence_ids.size();++i) {
            const auto child=std::ranges::find(*metadata,path.occurrence_ids[i],&assembly::OccurrenceSnapshot::occurrence_id);
            if(child==metadata->end()||child->manually_suppressed||child->dependency_suppressed)
                throw std::runtime_error("The original extrusion target is unavailable.");
            metadata=&child->children;
        }
        kernel::BodySnapshot snapshot=root->calculated_source;
        auto* children=&root->nested_snapshot;
        std::vector<kernel::KernelRigidPlacement> placements;
        const auto place=[&](const assembly::ComponentPlacement& p) {
            placements.push_back({{p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z}});
        };
        place(root->placement);
        std::size_t depth=1;
        while(snapshot->kernel_shape.empty()&&depth<path.occurrence_ids.size()) {
            const auto child=std::ranges::find(*children,path.occurrence_ids[depth],&assembly::OccurrenceSnapshot::occurrence_id);
            if(child==children->end()||child->manually_suppressed||child->dependency_suppressed)
                throw std::runtime_error("The original extrusion target is unavailable.");
            const auto body=snapshot->body_outputs.find(child->occurrence_id);
            if(body==snapshot->body_outputs.end())throw std::runtime_error("The original extrusion target is unavailable.");
            snapshot=body->second;place(child->placement);children=&child->children;++depth;
        }
        if(snapshot->kernel_shape.empty()||snapshot->source_fingerprint.empty())
            throw std::runtime_error("The original extrusion target is unavailable.");
        assembly::InstancePath relative;
        for(std::size_t i=depth;i<path.occurrence_ids.size();++i)relative=relative.child(path.occurrence_ids[i]);
        auto reference=patch.source_reference;reference.instance_path=relative.encoded();
        std::ranges::reverse(placements);
        patch.native_source=std::make_shared<const kernel::OrderedUpToSource>(
            kernel::OrderedUpToSource{std::move(snapshot),std::move(reference),std::move(placements)});
        patch.target.datum=false;
    };
    const auto request=[&](auto& value) {
        if constexpr(requires {value.ordered_end_limit;})
            if(value.ordered_end_limit)for(std::size_t row=0;row<value.ordered_end_limit->patches.size();++row)try {
                attach(value.ordered_end_limit->patches[row]);
            }catch(const std::runtime_error&) {
                throw kernel::OrderedUpToError(kernel::OrderedUpToFailure::InvalidInput,row,row);
            }
    };
    for(auto& operation:operations)std::visit([&](auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,kernel::FeatureGroupRequest>)
            for(auto& child:value.children)std::visit(request,child);
        else request(value);
    },operation.primitive);
}
} // namespace zima::workspace::detail
