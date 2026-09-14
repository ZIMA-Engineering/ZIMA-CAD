#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/dimension_layout.hpp>
namespace zima::workspace {
class ModelDimensionLayoutError : public std::runtime_error {
public:
    std::string code;
    ModelDimensionLayoutError(std::string code,const char* message):std::runtime_error(message),code(std::move(code)){}
};
struct ModelDimensionLayoutInfo {
    document::DimensionParameter parameter;
    std::optional<kernel::DimensionLayout> stored_layout;
};
// Metadata of current native parameter slots only. No body calculation, source
// loading or GUI-dependent dimension generation. Active Sketch edits keep their own API.
std::vector<document::DimensionParameter> model_dimension_parameters(const Workspace&,const std::string& document);
ModelDimensionLayoutInfo read_model_dimension_layout(const Workspace&,const std::string& document,const kernel::EdgeReference&);
// Same initial layout used by 3D Dimension Properties. A null text style inherits
// the original dimension's text; an explicit style is a complete replacement.
inline kernel::DimensionLayout default_model_dimension_layout(){return {0,8.0,0,0};}
bool set_model_dimension_layout(Workspace&,const std::string& document,const kernel::EdgeReference&,
    std::optional<kernel::DimensionLayout>);
}
