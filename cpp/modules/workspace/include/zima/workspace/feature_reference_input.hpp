#pragma once
#include <zima/workspace/placement_edit.hpp>
namespace zima::workspace {
// CLI input adapter only: prepare a private Part feature draft using the existing
// original-reference data and assignment/solver. The owning operation commits it.
[[nodiscard]] document::HistoryContainer prepare_part_feature_reference(Workspace&,
    const std::string& document,const std::string& container,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);
// Assembly cutters may refer to local construction or an exact Part occurrence.
// The prepared value is committed by the existing Assembly profile operation.
[[nodiscard]] document::HistoryContainer prepare_assembly_profile_reference(Workspace&,
    const std::string& document,const std::string& container,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);

}
