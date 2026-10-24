#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/relation_program.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <charconv>
#include <limits>
#include <cmath>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

namespace {
auto comparable_history(std::vector<zima::document::HistoryContainer> history) {
    // Regeneration writes embedded native Sketch JSON in its normal
    // formatted representation. Whitespace is not an authored modeling edit;
    // retain every parsed field, including signed floating-point zero.
    for(auto& item:history)zima::document::visit_feature_sketches(item,[](auto& sketch,std::size_t){sketch=nlohmann::json::parse(sketch).dump();});
    return history;
}
// Explicit offline authoring operation. Input must already satisfy the current
// native contract; the application retains no legacy reader or dual semantics.
std::map<std::string,double> reexpress_coordinates(zima::document::PartDocument& part) {
    using namespace zima;
    std::map<std::string,double> factors;
    std::set<std::pair<std::string,std::string>> bindings;
    const auto visit=[&](const sketcher::Sketch& sketch) {
        for(const auto& d:sketch.dimensions)if(sketcher::dimension_coordinate_sign(d,sketch.plane)<0) {
            const auto key="dimension:"+d.id;bindings.emplace(sketch.id,key);
            const auto name=part.dimension_identifiers.identifier(sketch.id,key);
            if(!name.empty())factors[name]=-1.;
        }
    };
    for(const auto& sketch:part.sketches)visit(sketch);
    for(const auto& feature:part.history)document::visit_feature_sketches(feature,[&](const auto& data,std::size_t){visit(sketcher::Sketch::from_serialized(data));});
    if(factors.empty()&&bindings.empty())return factors;
    if(!part.relations.empty())part.relations=document::RelationProgram(part.relations).reexpress_values(factors);
    auto table=document::parse_family_table(part.family_table);
    for(const auto& column:table.columns) {
        const auto binding=table.bindings.find(column);
        const bool flipped=binding==table.bindings.end()?factors.contains(column):
            binding->second.kind=="dimension"&&bindings.contains({binding->second.owner_id,binding->second.semantic_key});
        if(flipped)for(auto& row:table.instances)
            if(auto value=row.values.find(column);value!=row.values.end()&&!value->second.empty()) {
                double number{};const auto& text=value->second;
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
                if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(number))
                    throw std::runtime_error("Family coordinate must be a finite native number");
                char buffer[64];const auto formatted=std::to_chars(buffer,buffer+sizeof(buffer),-number,std::chars_format::general,std::numeric_limits<double>::max_digits10);
                if(formatted.ec!=std::errc{})throw std::runtime_error("Cannot re-express Family coordinate");
                value->second.assign(buffer,formatted.ptr);
            }
    }
    part.family_table=document::serialize_family_table(table);
    return factors;
}
// Embedded FORM definitions own their calculated native source, rather than
// referring to a factory file. Rebuild that source before its receiving Part;
// changing its Sketch contract invalidates the previous calculation key.
std::size_t reexpress_embedded_forms(zima::document::PartDocument& part,zima::kernel::OcctKernel& engine) {
    using namespace zima;
    std::size_t count{};
    for(auto& feature:part.history)if(feature.feature_kind==document::FeatureKind::SheetForm) {
        auto embedded=document::PartDocument::from_serialized(nlohmann::json::parse(*feature.sheet_form.definition));
        count+=reexpress_embedded_forms(embedded,engine);
        static_cast<void>(reexpress_coordinates(embedded));
        std::set<std::string> evaluated;
        for(const auto& [row,packet]:embedded.family.evaluated)evaluated.insert(row);
        embedded.family.evaluated.clear();
        const auto id=embedded.document_id;workspace::Workspace live;live.add_part(embedded);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;policy.refresh_physical_properties=true;
        static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
        for(const auto& row:document::parse_family_table(embedded.family_table).instances)
            if(evaluated.contains(row.id))static_cast<void>(workspace::open_family_instance(live,engine,id,row.name,false));
        const auto* current=live.open_part(id);
        if(!current||comparable_history(current->session.document().history)!=comparable_history(embedded.history)||
                current->session.document().body_history!=embedded.body_history||
                current->session.document().sketches.size()!=embedded.sketches.size())
            throw std::runtime_error("Embedded FORM conversion changed authored history or Body roles");
        for(std::size_t i=0;i<embedded.sketches.size();++i)
            if(embedded.sketches[i].serialized()!=current->session.document().sketches[i].serialized())
                throw std::runtime_error("Embedded FORM conversion changed authored Sketch geometry or references");
        feature.sheet_form.definition=std::make_shared<const std::string>(
            current->session.document().serialized(current->session.calculated_boundaries()).dump());
        ++count;
    }
    return count;
}
}

int main(int argc,char** argv){try{
    using namespace zima;
    if(argc==4&&std::string_view(argv[1])=="--audit-boundary-properties") {
        std::vector<kernel::BodyResult> boundaries;
        static_cast<void>(document::PartDocument::load(std::filesystem::u8path(argv[2]),&boundaries));
        kernel::OcctKernel engine;auto report=nlohmann::json::array();
        for(auto& boundary:boundaries) {
            engine.ensure_body_properties(boundary);
            report.push_back({{"volume",boundary.volume},{"area",boundary.surface_area},{"current",boundary.physical_properties_current}});
        }
        std::ofstream output(std::filesystem::u8path(argv[3]));output<<report.dump(2)<<'\n';return output?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-template-fixture") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        const auto prepare=[](auto&){}; // Preserve authored, already embedded glyph contours.
        const auto sketch=drawing::load_template_sketch(source,prepare);
        std::filesystem::create_directories(output.parent_path());drawing::save_template_sketch(sketch,output);
        if(drawing::load_template_sketch(output,prepare).serialized()!=sketch.serialized())
            throw std::runtime_error("Drawing template readback changed its authored Sketch");
        std::ofstream report(std::filesystem::u8path(argv[4]));report<<nlohmann::json{{"native_readback_exact",true},{"sketch_identity_unchanged",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-sketch-fixture") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::ifstream input(source);const std::string data((std::istreambuf_iterator<char>(input)),{});
        const auto sketch=sketcher::Sketch::from_serialized(data);sketch.validate();
        std::filesystem::create_directories(output.parent_path());std::ofstream saved(output);saved<<sketch.serialized();saved.close();
        std::ifstream restored(output);const std::string readback((std::istreambuf_iterator<char>(restored)),{});
        if(sketcher::Sketch::from_serialized(readback).serialized()!=sketch.serialized())
            throw std::runtime_error("Sketch fixture readback changed its definition");
        std::ofstream report(std::filesystem::u8path(argv[4]));report<<nlohmann::json{{"native_readback_exact",true},{"sketch_identity_unchanged",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-native-asset") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::filesystem::create_directories(output.parent_path());
        if(source.extension()==".asmz") {
            const auto value=assembly::AssemblyDocument::load(source);value.save(output);
            if(assembly::AssemblyDocument::load(output).serialized()!=value.serialized())
                throw std::runtime_error("Assembly authoring readback changed its definition");
        } else if(source.extension()==".drwz") {
            const auto value=drawing::DrawingDocument::load(source);value.save(output);
            std::ifstream first(output,std::ios::binary);const std::string original((std::istreambuf_iterator<char>(first)),{});first.close();
            auto readback=output;readback+=".readback.drwz";
            if(std::filesystem::exists(readback))throw std::runtime_error("Readback target must be unused");
            drawing::DrawingDocument::load(output).save(readback);
            std::ifstream second(readback,std::ios::binary);const std::string restored((std::istreambuf_iterator<char>(second)),{});second.close();
            std::filesystem::remove(readback);
            if(restored!=original)throw std::runtime_error("Drawing native readback changed its saved definition");
        } else throw std::runtime_error("Expected Assembly or Drawing asset");
        std::ofstream report(std::filesystem::u8path(argv[4]));
        report<<nlohmann::json{{"native_readback_exact",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==4&&std::string_view(argv[1])=="--write-assembly-template") {
        std::ifstream input(std::filesystem::u8path(argv[2]));nlohmann::json packet;input>>packet;
        auto value=assembly::AssemblyDocument::from_serialized(packet);
        if(!value.components.empty()||!value.cuts.empty()||!value.sketches.empty()||!value.constructions.empty())
            throw std::runtime_error("Start Assembly must have no authored history");
        const auto path=std::filesystem::u8path(argv[3]);value.save(path);
        if(assembly::AssemblyDocument::load(path).serialized()!=value.serialized())
            throw std::runtime_error("Assembly start template readback changed its definition");
        std::cout<<"Assembly start template written and reopened with the current serializer\n";
        return 0;
    }
    if(argc==5&&(std::string_view(argv[1])=="--regenerate-asset"||std::string_view(argv[1])=="--reexpress-coordinate-asset")) {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        const auto report=std::filesystem::u8path(argv[4]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::vector<kernel::BodyResult> cached;auto authored=document::PartDocument::load(source,&cached);
        kernel::OcctKernel engine;std::size_t embedded_forms{};
        std::map<std::string,double> coordinate_factors;
        if(std::string_view(argv[1])=="--reexpress-coordinate-asset") {
            embedded_forms=reexpress_embedded_forms(authored,engine);
            coordinate_factors=reexpress_coordinates(authored);
        }
        std::set<std::string> evaluated_rows;
        if(std::string_view(argv[1])=="--reexpress-coordinate-asset") {
            for(const auto& [row,packet]:authored.family.evaluated)evaluated_rows.insert(row);
            authored.family.evaluated.clear();
        }
        const auto id=authored.document_id;workspace::Workspace live;
        live.add_part(authored,cached,source);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;policy.refresh_physical_properties=true;
        static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
        if(!evaluated_rows.empty())for(const auto& row:document::parse_family_table(authored.family_table).instances)
            if(evaluated_rows.contains(row.id))static_cast<void>(workspace::open_family_instance(live,engine,id,row.name,false));
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
        evidence["coordinate_reexpression"]=coordinate_factors;
        evidence["embedded_form_definitions_recalculated"]=embedded_forms;
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
