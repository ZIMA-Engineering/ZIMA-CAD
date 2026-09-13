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
    const std::string& document_id,document::HistoryContainer,EdgeTreatmentEditMode);
} // namespace zima::workspace
