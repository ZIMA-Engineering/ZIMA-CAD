#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>
namespace zima::workspace {
class EdgeTreatmentOperationError : public std::runtime_error {
public:
    EdgeTreatmentOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class EdgeTreatmentEditMode { Create, Replace };
// Two endpoints only for one connected, unbranched open path of exact input
// identities. No spatial welding, kernel traversal or invented endpoints.
[[nodiscard]] std::vector<kernel::VertexReference> edge_treatment_route_endpoints(
    const std::vector<kernel::ViewerEdge>&,const std::vector<kernel::EdgeReference>&);
// Exact, disposable Properties preview.  It evaluates the pending definition
// at its own history boundary without committing it or publishing an Undo
// state.  An unchanged edit reuses the already calculated output packet.
[[nodiscard]] kernel::ViewerMesh edge_treatment_preview_mesh(
    const document::DocumentSession&,const kernel::OcctKernel&,
    const document::HistoryContainer&);
struct EdgeTreatmentEndCutChoice {
    // Parallel to the requested route endpoints. An empty reference selects
    // their ordinary exact automatic cut; a populated reference selects one
    // complete alternative discovered from the calculated native topology.
    std::vector<kernel::FaceReference> faces;
};
[[nodiscard]] std::vector<EdgeTreatmentEndCutChoice>
edge_treatment_end_cut_choices(
    const document::DocumentSession&,const kernel::OcctKernel&,
    const document::HistoryContainer&,
    const std::vector<std::pair<std::size_t,bool>>& route_ends);
[[nodiscard]] bool commit_edge_treatment(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,EdgeTreatmentEditMode,
    const std::vector<kernel::DimensionLayoutEntry>& layouts = {});
// Removes one persisted edge or the whole user-defined route. Returns true
// when the last route removes the history container. Downstream calculation
// errors then follow the existing history-delete contract and remain undoable.
[[nodiscard]] bool remove_edge_treatment_selection(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,const std::string& container_id,std::size_t route,
    std::optional<kernel::EdgeReference> edge=std::nullopt);
} // namespace zima::workspace
