#include <zima/kernel/step_source_document.hpp>

#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <cstdio>
#include <cstdlib>

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
    const bool profile=std::getenv("ZIMA_CPP_IMPORT_PROFILE")!=nullptr;
    auto started=std::chrono::steady_clock::now();
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF",impl_->document);
    if(impl_->reader.ReadFile(impl_->path.c_str())!=IFSelect_RetDone)
        throw std::runtime_error(failure_message);
    if(profile)std::fprintf(stderr,"STEP source reading: %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    started=std::chrono::steady_clock::now();
    impl_->reader.ChangeReader().SetSystemLengthUnit(1.0);
    if(!impl_->reader.Transfer(impl_->document))throw std::runtime_error(failure_message);
    if(profile)std::fprintf(stderr,"STEP source OCCT transfer: %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
}
StepSourceDocument::~StepSourceDocument()=default;
StepSourceDocument::StepSourceDocument(StepSourceDocument&&) noexcept=default;
StepSourceDocument& StepSourceDocument::operator=(StepSourceDocument&&) noexcept=default;
const std::string& StepSourceDocument::source_path() const { return impl_->path; }
const STEPCAFControl_Reader& StepSourceDocument::reader() const { return impl_->reader; }
TDocStd_Document* StepSourceDocument::document() const { return impl_->document.get(); }

}
