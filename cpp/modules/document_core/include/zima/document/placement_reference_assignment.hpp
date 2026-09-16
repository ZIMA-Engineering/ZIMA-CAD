#pragma once
#include <zima/document/placement_types.hpp>
#include <algorithm>
#include <array>

namespace zima::document {
// Assemble the exact pending reference set, excluding both halves of a
// replaced positional source. Shared by the dialog and command adapters.
inline std::vector<ConstructionReference> combined_placement_references(
        const std::vector<ConstructionReference>& position,
        const std::vector<ConstructionReference>& orientation,
        std::optional<std::size_t> skip = {}) {
    const auto populated=[](const auto& ref){return !ref.owner_id.empty() || !ref.semantic_key.empty();};
    std::vector<ConstructionReference> result;
    for(std::size_t i=0;i<position.size();++i)
        if(skip!=i && populated(position[i]))result.push_back(position[i]);
    for(std::size_t i=0;i<orientation.size();++i) {
        if(skip==i+3 || !populated(orientation[i]))continue;
        if(skip && *skip<3 && *skip<position.size()) {
            const auto& removed=position[*skip];const auto& ref=orientation[i];
            if(ref.owner_id==removed.owner_id && ref.semantic_key==removed.semantic_key &&
                    ref.instance_path==removed.instance_path)continue;
        }
        result.push_back(orientation[i]);
    }
    return result;
}
// A view of pending dialog/command rows. Position and FRONT/TOP rows have
// independent ownership; this operation changes only the caller's draft.
struct PlacementReferenceRows {
    std::vector<ConstructionReference>& position;
    std::vector<ConstructionReference>& orientation;
    std::array<bool,3>& empty_position_locks;
};
enum class PlacementReferenceError { None, InvalidSlot, Duplicate, MissingMeasuredOffset };
struct PlacementReferenceAssignment {
    PlacementReferenceError error{PlacementReferenceError::None};
    std::optional<std::size_t> mirrored_orientation;
};
[[nodiscard]] inline PlacementReferenceAssignment assign_placement_reference(
    PlacementReferenceRows rows,bool with_orientation,std::size_t index,
    ConstructionReference reference,bool derive_orientation=true) {
    // Duplicate checks also run after the reference has moved into its row.
    const auto path=reference.instance_path,owner=reference.owner_id,key=reference.semantic_key;
    const auto duplicate=[&](const auto& existing) {
        return existing.instance_path==path&&existing.owner_id==owner&&existing.semantic_key==key;
    };
    if(index>=3) {
        const auto slot=index-3;
        if(!with_orientation||slot>=2)return {PlacementReferenceError::InvalidSlot,{}};
        if(std::any_of(rows.orientation.begin(),rows.orientation.end(),duplicate))return {PlacementReferenceError::Duplicate,{}};
        reference.orientation_drives_rotation=true;reference.orientation_role=slot==0?"front":"top";reference.orientation_only=true;
        if(rows.orientation.size()<=slot)rows.orientation.resize(slot+1);
        rows.orientation[slot]=std::move(reference);return {};
    }
    for(std::size_t i=0;i<rows.position.size();++i)
        if(i!=index&&duplicate(rows.position[i]))return {PlacementReferenceError::Duplicate,{}};
    if(rows.position.size()<=index)rows.position.resize(index+1);
    const bool capture_once=rows.position[index].semantic_key.empty()&&rows.empty_position_locks[index];
    const bool locked=!rows.position[index].semantic_key.empty()?rows.position[index].offset_locked:rows.empty_position_locks[index];
    if(reference.supports_offset) {
        reference.offset_locked=locked&&!capture_once;
        if(locked&&!reference.measured_offset)return {PlacementReferenceError::MissingMeasuredOffset,{}};
        if(locked)reference.offset=*reference.measured_offset;
    } else {reference.offset=0;reference.offset_locked=true;}
    const auto previous = rows.position[index];
    if (previous.owner_id != reference.owner_id || previous.semantic_key != reference.semantic_key ||
            previous.instance_path != reference.instance_path) {
        for (auto& oriented : rows.orientation)
            if (oriented.owner_id == previous.owner_id && oriented.semantic_key == previous.semantic_key &&
                    oriented.instance_path == previous.instance_path) oriented = {};
    }
    rows.empty_position_locks[index]=false;reference.measured_offset.reset();rows.position[index]=std::move(reference);
    PlacementReferenceAssignment result;
    if(with_orientation&&derive_orientation&&rows.position[index].supports_offset) {
        if(rows.orientation.size()<2)rows.orientation.resize(2);
        const auto same_source=[&](const auto& existing) {
            return !existing.owner_id.empty()&&duplicate(existing);
        };
        if(!std::any_of(rows.orientation.begin(),rows.orientation.end(),same_source)) {
            // A positional edge may already own FRONT without an orientation
            // table twin. Never mirror the following plane into that FRONT.
            const auto& positional = rows.position[index];
            auto empty = rows.orientation.end();
            if (positional.orientation_drives_rotation &&
                    (positional.orientation_role == "front" || positional.orientation_role == "top")) {
                const auto slot = positional.orientation_role == "front" ? 0 : 1;
                if (rows.orientation[slot].owner_id.empty() && rows.orientation[slot].semantic_key.empty())
                    empty = rows.orientation.begin() + slot;
            } else {
                empty = std::find_if(rows.orientation.begin(),rows.orientation.end(),[](const auto& value){return value.owner_id.empty()&&value.semantic_key.empty();});
            }
            if(empty!=rows.orientation.end()) {
                const auto slot=static_cast<std::size_t>(empty-rows.orientation.begin());*empty=rows.position[index];
                empty->orientation_drives_rotation=true;empty->orientation_role=slot==0?"front":"top";empty->orientation_only=true;
                result.mirrored_orientation=slot;
            }
        }
    }
    return result;
}

struct PlacementReferenceRemoval {
    PlacementReferenceError error{PlacementReferenceError::None};
    bool changed{};
    std::optional<std::size_t> paired_orientation;
};
// Keep holes in pending positional rows. Persistent callers compact their
// combined references only when committing, just like the Properties dialog.
[[nodiscard]] inline PlacementReferenceRemoval remove_placement_reference(
    PlacementReferenceRows rows, bool with_orientation, std::size_t index) {
    const auto empty = [](const auto& ref) {
        return ref.owner_id.empty() && ref.semantic_key.empty();
    };
    if (index >= 3) {
        const auto slot = index - 3;
        if (!with_orientation || slot >= 2)
            return {PlacementReferenceError::InvalidSlot, false, {}};
        if (slot >= rows.orientation.size() || empty(rows.orientation[slot])) return {};
        rows.orientation[slot] = {};
        return {PlacementReferenceError::None, true, {}};
    }
    if (index >= rows.position.size() || empty(rows.position[index])) return {};
    const auto removed = rows.position[index];
    rows.position[index] = {};
    rows.empty_position_locks[index] = false;
    PlacementReferenceRemoval result{PlacementReferenceError::None, true, {}};
    if (with_orientation) {
        const auto paired = std::find_if(rows.orientation.begin(), rows.orientation.end(),
            [&](const auto& ref) {
                return ref.owner_id == removed.owner_id && ref.semantic_key == removed.semantic_key &&
                    ref.instance_path == removed.instance_path;
            });
        if (paired != rows.orientation.end()) {
            result.paired_orientation = static_cast<std::size_t>(paired - rows.orientation.begin());
            rows.orientation.erase(paired);
            for (std::size_t slot = 0; slot < rows.orientation.size(); ++slot)
                rows.orientation[slot].orientation_role = slot == 0 ? "front" : "top";
        }
    }
    while (!rows.position.empty() && empty(rows.position.back())) rows.position.pop_back();
    return result;
}
}
