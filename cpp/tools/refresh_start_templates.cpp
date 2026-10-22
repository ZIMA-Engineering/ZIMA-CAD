#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

namespace {
auto comparable_history(std::vector<zima::document::HistoryContainer> history) {
    // Bend regeneration writes embedded native Sketch JSON in its normal
    // formatted representation. Whitespace is not an authored modeling edit;
    // retain every parsed field, including signed floating-point zero.
    for(auto& item:history)if(item.feature_kind==zima::document::FeatureKind::Bend)
        for(auto& sketch:item.bend.auxiliary_sketches)sketch=nlohmann::json::parse(sketch).dump();
    return history;
}
}

int main(int argc,char** argv){try{
    using namespace zima;
    if(argc==5&&std::string_view(argv[1])=="--regenerate-asset") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        const auto report=std::filesystem::u8path(argv[4]);
        std::vector<kernel::BodyResult> cached;auto authored=document::PartDocument::load(source,&cached);
        const auto id=authored.document_id;workspace::Workspace live;
        live.add_part(authored,cached,source);kernel::OcctKernel engine;
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;policy.refresh_physical_properties=true;
        static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
        const auto* current=live.open_part(id);if(!current)throw std::runtime_error("Factory asset disappeared");
        const auto& updated=current->session.document();const auto& calculated=current->session.calculated_boundaries();
        if(comparable_history(updated.history)!=comparable_history(authored.history)||updated.body_history!=authored.body_history||updated.sketches.size()!=authored.sketches.size()) {
            std::filesystem::create_directories(report.parent_path());
            std::ofstream diagnostic(report);diagnostic<<nlohmann::json{{"authored_definition_changes",
                nlohmann::json::diff(authored.serialized(),updated.serialized())}}.dump(2)<<'\n';
            throw std::runtime_error("Factory regeneration changed authored history, sides or Body ownership");
        }
        for(std::size_t i=0;i<authored.sketches.size();++i)
            if(authored.sketches[i].serialized()!=updated.sketches[i].serialized())
                throw std::runtime_error("Factory regeneration changed authored Sketch geometry or references");
        std::filesystem::create_directories(output.parent_path());updated.save(output,calculated);
        std::vector<kernel::BodyResult> restored;const auto reopened=document::PartDocument::load(output,&restored);
        if(reopened.serialized()!=updated.serialized()||restored.size()!=calculated.size())
            throw std::runtime_error("Factory native readback changed definition or boundary count");
        for(std::size_t i=0;i<calculated.size();++i)
            if(document::serialize_body_result(restored[i])!=document::serialize_body_result(calculated[i]))
                throw std::runtime_error("Factory native readback lost geometry or side identities");
        nlohmann::json evidence={{"calculated_boundaries",calculated.size()},{"history_count",updated.history.size()},
            {"authored_history_unchanged",true},{"sketches_unchanged",true},{"native_readback_exact",true}};
        if(!calculated.empty()) {
            evidence["volume"]=calculated.back().volume;evidence["area"]=calculated.back().surface_area;
            evidence["physical_properties_current"]=calculated.back().physical_properties_current;
            evidence["errors"]=calculated.back().calculation_errors;
        }
        if(!cached.empty()){evidence["previous_volume"]=cached.back().volume;evidence["previous_area"]=cached.back().surface_area;}
        evidence["parameters"]=updated.serialized().value("user_parameter_values",nlohmann::json::object());
        std::ofstream stream(report);stream<<evidence.dump(2)<<'\n';stream.close();
        if(!stream)throw std::runtime_error("Cannot write factory verification evidence");
        return 0;
    }
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
