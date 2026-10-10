#pragma once

#include <memory>
#include <string>

class STEPCAFControl_Reader;
class TDocStd_Document;

namespace zima::kernel {

// One explicit import owns one transferred source document. Inspection and
// source capture may share it; it never becomes a persistent filename cache.
class StepSourceDocument final {
public:
    explicit StepSourceDocument(std::string path,
        const char* failure_message = "OCCT STEP product structure import failed");
    ~StepSourceDocument();
    StepSourceDocument(const StepSourceDocument&) = delete;
    StepSourceDocument& operator=(const StepSourceDocument&) = delete;
    StepSourceDocument(StepSourceDocument&&) noexcept;
    StepSourceDocument& operator=(StepSourceDocument&&) noexcept;
    [[nodiscard]] const std::string& source_path() const;
    [[nodiscard]] const STEPCAFControl_Reader& reader() const;
    [[nodiscard]] TDocStd_Document* document() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
