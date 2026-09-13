#pragma once
#include <zima/workspace/measurement_operations.hpp>
namespace zima::workspace {
class MeasurementOperationError : public std::runtime_error {
public:
    const char* code;
    std::optional<std::size_t> reference_index;
    MeasurementOperationError(const char* code,const char* message,std::optional<std::size_t> index={})
        :std::runtime_error(message),code(code),reference_index(index){}
};
struct MeasurementEdit {
    std::string document_id;
    std::uint64_t revision{},generation{};
    std::shared_ptr<const int> runtime_identity;
    bool creating{};
    kernel::SavedMeasurement initial;
};
// Reading/preparing an inspector never writes a document or evaluates its bodies.
[[nodiscard]] MeasurementEdit prepare_measurement_edit(const Workspace&,const std::string& document,const std::string& object={},const std::string& name_prefix="Measurement");
// Replace only values/distance, resolving persisted reference identities afresh.
void evaluate_measurement_references(const Workspace&,const std::string& document,kernel::SavedMeasurement&);
[[nodiscard]] bool commit_measurement(Workspace&,const MeasurementEdit&,kernel::SavedMeasurement);
[[nodiscard]] bool remove_measurement(Workspace&,const std::string& document,const std::string& object);
}
