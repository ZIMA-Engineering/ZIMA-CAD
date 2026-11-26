#pragma once
#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {
enum class CamResultState { Current, NotCalculated, Stale, MissingSource, SuppressedSource,
    InvalidSource, UnresolvedFrame, Collision };
[[nodiscard]] BodyGraphEdit prepare_cam_edit(const document::PartDocument&,const std::string& body_id={});
[[nodiscard]] std::vector<std::string> cam_source_candidates(const document::DocumentSession&,
    const BodyGraphEdit&,bool stock);
[[nodiscard]] std::vector<std::string> cam_clamping_candidates(const document::DocumentSession&,const BodyGraphEdit&);
[[nodiscard]] CamResultState cam_result_state(const document::DocumentSession&,const std::string& body_id);
[[nodiscard]] kernel::ViewerReferenceGeometry cam_placement_geometry(const document::DocumentSession&,
    const BodyGraphEdit&);
// Definition changes are metadata transactions; calculated material remains immutable.
[[nodiscard]] bool commit_cam(Workspace&,const BodyGraphEdit&,document::BodyHistory);

struct CamSketchInput {sketcher::Sketch sketch;document::Placement body_frame;};
struct CamCalculationInput {
    std::string document_id,body_id,intent_key;
    std::shared_ptr<const int> runtime_identity;
    std::uint64_t revision{},generation{};
    document::CamDefinition definition;
    // Material is in document coordinates. Calculation transforms private copies
    // into the work frame and publishes remaining material in document coordinates.
    std::vector<std::pair<std::string,kernel::BodySnapshot>> material;
    // Exact deduplicated clamping and machining profile inputs.
    std::vector<CamSketchInput> clamping_sketches;
    std::vector<std::pair<std::string,std::string>> source_keys;
};
// Preparing immutable inputs never invokes the kernel. Publication rejects results
// from another revision, runtime, definition or source generation without an Undo.
[[nodiscard]] CamCalculationInput prepare_cam_calculation(const Workspace&,const std::string& document_id,
    const std::string& body_id);
[[nodiscard]] bool publish_cam_calculation(Workspace&,const CamCalculationInput&,document::CamCalculated);
} // namespace zima::workspace
