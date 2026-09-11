#pragma once
#include <zima/workspace/workspace.hpp>
#include <functional>
#include <stdexcept>

namespace zima::workspace {
class ReferenceQueryError : public std::runtime_error {
public:
    ReferenceQueryError(const char* code,const char* message) : std::runtime_error(message),code(code) {}
    const char* code;
};
struct ReferenceFrame {
    std::string instance_prefix;
    bool visible{true};
    bool suppressed{};
    std::function<kernel::Vec3(kernel::Vec3)> point=[](auto p){return p;};
    std::function<kernel::Vec3(kernel::Vec3)> direction=[](auto p){return p;};
    // Analytic face data stays in its source occurrence frame; mesh samples
    // and spline poles in an Assembly snapshot already include nested placement.
    std::function<kernel::Vec3(const std::string&,kernel::Vec3)> surface_point=[](const auto&,auto p){return p;};
    std::function<kernel::Vec3(const std::string&,kernel::Vec3)> surface_direction=[](const auto&,auto p){return p;};
    [[nodiscard]] std::string path(const std::string& local) const;
};
// Synchronous borrowed packets. The visitor must not retain packet references.
// Returning false stops iteration. Original geometry is never rebuilt from OCCT,
// copied from result topology or refreshed from an open dependency implicitly.
using ReferenceVisitor=std::function<bool(const kernel::ViewerReferenceGeometry&,const ReferenceFrame&)>;
void visit_original_references(const Workspace&,const std::string& document,const ReferenceVisitor&);
}
