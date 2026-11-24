#pragma once

#include <zima/workspace/model_calculation.hpp>
#include <memory>

namespace zima::workspace {

// One interactive Properties session. Failed calculations remain private;
// only an unchanged second submission can reuse them in the ordinary command
// transaction. Native commands without this scope retain their own policies.
class ModelingConfirmation final {
public:
    ModelingConfirmation();
    ~ModelingConfirmation();
    void clear();
    [[nodiscard]] const std::string& failure() const;
private:
    struct Pending;
    std::unique_ptr<Pending> pending_;
    friend class ModelingConfirmationAttempt;
};

class ModelingConfirmationAttempt final {
public:
    ModelingConfirmationAttempt(ModelingConfirmation&, const Workspace&,
        const std::string& document_id);
    ~ModelingConfirmationAttempt();
    ModelingConfirmationAttempt(const ModelingConfirmationAttempt&) = delete;
    ModelingConfirmationAttempt& operator=(const ModelingConfirmationAttempt&) = delete;
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] bool accepted(const std::string& owner = {}) const;
    [[nodiscard]] bool applies(const document::PartDocument&) const;
    [[nodiscard]] bool applies(const assembly::AssemblyDocument&) const;
    using AssemblyCalculation = std::function<void(assembly::AssemblyDocument&, bool)>;
    void calculate(assembly::AssemblyDocument&, const AssemblyCalculation&);
    using Calculation = std::function<std::vector<kernel::BodyResult>(
        document::PartDocument&, const PartCalculationPolicy&)>;
    std::vector<kernel::BodyResult> calculate(document::PartDocument&,
        const PartCalculationPolicy&, const Calculation&);
    [[nodiscard]] static ModelingConfirmationAttempt* current();
    // Metadata-owned FEM geometry uses the same dialog acknowledgement and
    // source-generation guard without calculating or copying a CAD Body.
    using FemValidation = std::function<void(document::FemCondition&)>;
    void validate(document::FemCondition&, const std::string& body_id,
        const FemValidation&);
private:
    ModelingConfirmation& confirmation_;
    const Workspace& workspace_;
    std::string document_id_;
    std::set<std::string> accepted_owners_;
    bool failed_{};
    ModelingConfirmationAttempt* previous_{};
};

[[nodiscard]] bool confirmed_unresolved_modeling_definition(const std::string& owner);

} // namespace zima::workspace
