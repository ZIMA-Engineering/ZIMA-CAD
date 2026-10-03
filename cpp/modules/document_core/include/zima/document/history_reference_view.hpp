#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace zima::document {

// Calculation input, not a new reference owner. A state evaluator supplies
// complete correspondence geometry for the selected original owners before
// each affected history entry. Stored reference identities remain unchanged.
struct HistoryReferenceView {
    using Owner = std::pair<std::string,std::string>; // owner ID, occurrence path
    std::set<Owner> owners;
    kernel::ViewerReferenceGeometry geometry;
};
using HistoryReferenceViews = std::map<std::string,HistoryReferenceView>;

namespace history_reference_detail {
inline void retain(kernel::ViewerReferenceGeometry& geometry,
                   const std::set<HistoryReferenceView::Owner>& owners,bool selected) {
    const auto keep=[&](const auto& ref) {
        return owners.contains({ref.owner_id,ref.instance_path})==selected;
    };
    std::erase_if(geometry.edges,[&](const auto& item){return !keep(item.reference);});
    std::erase_if(geometry.points,[&](const auto& item){return !keep(item.reference);});
    std::erase_if(geometry.axes,[&](const auto& item){return !keep(item.reference);});
    std::size_t kept=0;
    for(std::size_t i=0;i<geometry.triangle_references.size();++i)if(keep(geometry.triangle_references[i])) {
        if(i!=kept) {
            geometry.triangle_references[kept]=std::move(geometry.triangle_references[i]);
            for(std::size_t k=0;k<3;++k)geometry.triangles[3*kept+k]=geometry.triangles[3*i+k];
        }
        ++kept;
    }
    geometry.triangle_references.resize(kept);geometry.triangles.resize(kept*3);
    // Do not accumulate unused mesh vertices when stepping through history.
    std::vector<std::uint32_t> remap(geometry.vertices.size(),std::numeric_limits<std::uint32_t>::max());
    std::vector<kernel::Vec3> vertices;vertices.reserve(std::min(geometry.vertices.size(),geometry.triangles.size()));
    for(auto& index:geometry.triangles) {
        auto& target=remap.at(index);
        if(target==std::numeric_limits<std::uint32_t>::max()) {
            target=static_cast<std::uint32_t>(vertices.size());vertices.push_back(geometry.vertices[index]);
        }
        index=target;
    }
    geometry.vertices=std::move(vertices);
}
inline void append(kernel::ViewerReferenceGeometry& target,const kernel::ViewerReferenceGeometry& source) {
    const auto offset=static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),source.vertices.begin(),source.vertices.end());
    for(auto index:source.triangles)target.triangles.push_back(offset+index);
    target.triangle_references.insert(target.triangle_references.end(),source.triangle_references.begin(),source.triangle_references.end());
    target.edges.insert(target.edges.end(),source.edges.begin(),source.edges.end());
    target.points.insert(target.points.end(),source.points.begin(),source.points.end());
    target.axes.insert(target.axes.end(),source.axes.begin(),source.axes.end());
}
}

// An entry without a view uses its original input, even after an entry with a
// view. Repeated evaluation therefore cannot leak a later state backwards.
// Resolved datums belonging to other owners remain available to descendants.
class HistoryReferenceResolver {
public:
    HistoryReferenceResolver(const kernel::ViewerReferenceGeometry& original,
                             const HistoryReferenceViews& views):views_(views) {
        for(const auto& [entry,view]:views) {
            if(entry.empty())invalid();
            for(const auto& owner:view.owners)if(owner.first.empty())invalid();
            const auto valid=[&](const auto& ref){return view.owners.contains({ref.owner_id,ref.instance_path});};
            const auto& geometry=view.geometry;
            if(geometry.triangles.size()!=geometry.triangle_references.size()*3)invalid();
            for(auto index:geometry.triangles)if(index>=geometry.vertices.size())invalid();
            for(const auto& ref:geometry.triangle_references)if(!valid(ref))invalid();
            for(const auto& item:geometry.edges)if(!valid(item.reference))invalid();
            for(const auto& item:geometry.points)if(!valid(item.reference))invalid();
            for(const auto& item:geometry.axes)if(!valid(item.reference))invalid();
            owners_.insert(view.owners.begin(),view.owners.end());
        }
        if(owners_.empty())return;
        original_=original;
        if(original_.triangles.size()!=original_.triangle_references.size()*3)invalid();
        for(auto index:original_.triangles)if(index>=original_.vertices.size())invalid();
        history_reference_detail::retain(original_,owners_,true);
    }
    void enter(const std::string& entry,kernel::ViewerReferenceGeometry& geometry) const {
        if(owners_.empty())return;
        auto replacement=original_;
        if(const auto found=views_.find(entry);found!=views_.end()) {
            history_reference_detail::retain(replacement,found->second.owners,false);
            history_reference_detail::append(replacement,found->second.geometry);
        }
        history_reference_detail::retain(geometry,owners_,false);
        history_reference_detail::append(geometry,replacement);
    }
private:
    [[noreturn]] static void invalid(){throw std::invalid_argument("History reference view is invalid.");}
    const HistoryReferenceViews& views_;
    std::set<HistoryReferenceView::Owner> owners_;
    kernel::ViewerReferenceGeometry original_;
};
}
