#include <zima/workspace/native_documents.hpp>
#include <iostream>

int main(){try{
    using namespace zima;
    const auto root=std::filesystem::current_path()/"config/templates";
    for(const auto* name:{"START_PART.prtz","START_SKELETON.prtz"}) {
        const auto path=root/name;auto part=document::PartDocument::load(path);
        if(!part.history.empty()||!part.body_history.order().empty())throw std::runtime_error("Start template must have no modeling history");
        part.save(path);static_cast<void>(document::PartDocument::load(path));
    }
    const auto path=root/"START_ASSEMBLY.asmz";auto assembly=assembly::AssemblyDocument::load(path);
    if(!assembly.components.empty())throw std::runtime_error("Start Assembly must have no components");
    assembly.save(path);static_cast<void>(assembly::AssemblyDocument::load(path));
    std::cout<<"Native start templates rewritten and reopened with the current serializer\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
