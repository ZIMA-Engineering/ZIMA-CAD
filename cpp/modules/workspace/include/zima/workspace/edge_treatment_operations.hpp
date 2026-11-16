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
