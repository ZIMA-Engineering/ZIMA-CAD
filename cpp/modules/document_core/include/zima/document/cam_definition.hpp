#pragma once
#include <zima/cam/serialization.hpp>
#include <zima/document/placement_types.hpp>
#include <memory>
#include <variant>

namespace zima::document {
struct CamManualOperation {
    std::string id,name;
    cam::Program program;
    bool operator==(const CamManualOperation&) const = default;
};
using CamOperation=std::variant<CamManualOperation,cam::Facing>;
[[nodiscard]] const std::string& cam_operation_id(const CamOperation&);
[[nodiscard]] const std::string& cam_operation_name(const CamOperation&);

struct CamCalculated {
    std::string id,intent_key;
    std::vector<std::pair<std::string,std::string>> sources;
    cam::Program program;
    cam::Machine machine;
    cam::Point machine_work_origin;
    std::vector<cam::Tool> tools;
    Placement work_frame;
    kernel::BodySnapshot remaining;
    std::vector<cam::Collision> collisions;
    double removed_mm3{},missing_target_mm3{},tolerance_mm{1e-7};
    bool operator==(const CamCalculated&) const;
};
struct CamDefinition {
    std::string stock_id,target_id;
    std::vector<std::string> fixture_ids,clamping_sketch_ids;
    Placement work_frame;
    cam::Machine machine;
    cam::Point machine_work_origin;
    std::vector<cam::Tool> tools;
    std::vector<CamOperation> operations;
    cam::Units display_units{cam::Units::Millimetres};
    double tolerance_mm{1e-7};
    // Immutable calculated data is shared by Undo and persisted inside the Part.
    // Retain stale output for inspection, but never accept it as downstream stock.
    std::shared_ptr<const CamCalculated> calculated;
    bool operator==(const CamDefinition&) const;
};
void validate_cam_definition(const CamDefinition&);
// Full result validation belongs to native loading or explicit publication,
// never repeated Body graph/Properties validation of an immutable result.
void validate_cam_calculated(const CamCalculated&);
[[nodiscard]] std::string cam_intent_key(const CamDefinition&);
[[nodiscard]] bool cam_result_matches_definition(const CamDefinition&);
[[nodiscard]] bool cam_result_has_collisions(const CamCalculated&);
void to_json(nlohmann::json&,const CamDefinition&);
void from_json(const nlohmann::json&,CamDefinition&);
} // namespace zima::document
