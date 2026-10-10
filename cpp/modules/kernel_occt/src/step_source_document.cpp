#include <zima/kernel/step_source_document.hpp>

#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <stdexcept>
#include <utility>

namespace zima::kernel {

struct StepSourceDocument::Impl {
    std::string path;
    Handle(TDocStd_Document) document;
    STEPCAFControl_Reader reader;
    ~Impl() {
        if(!document.IsNull())XCAFApp_Application::GetApplication()->Close(document);
    }
};

StepSourceDocument::StepSourceDocument(std::string path,const char* failure_message)
    : impl_(std::make_unique<Impl>()) {
    impl_->path=std::move(path);
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",impl_->document);
    if(impl_->reader.ReadFile(impl_->path.c_str())!=IFSelect_RetDone)
        throw std::runtime_error(failure_message);
    impl_->reader.ChangeReader().SetSystemLengthUnit(1.0);
    if(!impl_->reader.Transfer(impl_->document))throw std::runtime_error(failure_message);
}
StepSourceDocument::~StepSourceDocument()=default;
StepSourceDocument::StepSourceDocument(StepSourceDocument&&) noexcept=default;
StepSourceDocument& StepSourceDocument::operator=(StepSourceDocument&&) noexcept=default;
const std::string& StepSourceDocument::source_path() const { return impl_->path; }
const STEPCAFControl_Reader& StepSourceDocument::reader() const { return impl_->reader; }
TDocStd_Document* StepSourceDocument::document() const { return impl_->document.get(); }

}
