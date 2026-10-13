#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

int main(int argc,char** argv){try{
    using namespace zima;
    const auto root=std::filesystem::current_path()/"config/templates";
    for(const auto* name:{"START_PART_mm.prtz","START_PART_in.prtz","START_SKELETON_mm.prtz","START_SKELETON_in.prtz"}) {
        const auto path=root/name;auto part=document::PartDocument::load(path);
        if(!part.history.empty()||!part.body_history.order().empty())throw std::runtime_error("Start template must have no modeling history");
        part.save(path);static_cast<void>(document::PartDocument::load(path));
    }
    for(const auto* name:{"START_ASSEMBLY_mm.asmz","START_ASSEMBLY_in.asmz"}) {
    const auto path=root/name;auto assembly=assembly::AssemblyDocument::load(path);
    if(!assembly.components.empty())throw std::runtime_error("Start Assembly must have no components");
    assembly.save(path);static_cast<void>(assembly::AssemblyDocument::load(path));
    }
    if(argc==2&&std::string_view(argv[1])=="--all-factory") {
        kernel::OcctKernel engine;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root.parent_path())) {
            if(!entry.is_regular_file()||entry.path().parent_path()==root)continue;
            const auto extension=entry.path().extension();
            if(extension!=".prtz"&&extension!=".frmz"&&extension!=".tblz"&&extension!=".symz")continue;
            const auto part=document::PartDocument::load(entry.path());
            const auto authored=part.serialized();
            const auto calculated=engine.evaluate_history(part.kernel_operations());
            part.save(entry.path(),calculated);
            std::vector<kernel::BodyResult> restored;
            const auto reopened=document::PartDocument::load(entry.path(),&restored);
            if(reopened.serialized()!=authored||restored.size()!=calculated.size())
                throw std::runtime_error("Factory asset changed its authored definition or cache count: "+entry.path().string());
            for(std::size_t i=0;i<calculated.size();++i)
                if(document::serialize_body_result(restored[i])!=document::serialize_body_result(calculated[i]))
                    throw std::runtime_error("Factory asset lost calculated geometry or reference data: "+entry.path().string());
            std::cout<<entry.path().filename().string()<<": calculated, saved and reopened\n";
        }
    }
    std::cout<<"Native start templates rewritten and reopened with the current serializer\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
