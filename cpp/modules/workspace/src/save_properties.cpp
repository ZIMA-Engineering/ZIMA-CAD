#include <zima/workspace/save_properties.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/body_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <zima/kernel/occt_kernel.hpp>

namespace zima::workspace {
bool share_family_source_analysis(assembly::AssemblyDocument& member,const assembly::AssemblyDocument& owner) {
    bool complete=true;
    for(auto& component:member.components) {
        if(member.owns_component_result(component.occurrence_id))continue;
        const auto source=std::ranges::find_if(owner.components,[&](const auto& candidate) {
            return candidate.source_document_id==component.source_document_id&&candidate.source_kind==component.source_kind&&
                !owner.owns_component_result(candidate.occurrence_id);
        });
        if(source==owner.components.end()){complete=false;continue;}
        component.calculated_source=source->calculated_source;
        component.density_kg_mm3=source->density_kg_mm3;
        component.nested_mass_kg=source->nested_mass_kg;
        component.mass_volume_mm3=source->mass_volume_mm3;
        component.source_missing=source->source_missing;
    }
    return complete;
}
bool refresh_save_properties(document::PartDocument& doc,std::vector<kernel::BodyResult>& boundaries) {
    bool changed=complete_part_physical_analysis(
        kernel::OcctKernel{},doc,boundaries,true);
    for(auto& [row,packet]:doc.family.evaluated) {
        std::vector<kernel::BodyResult> cache;
        auto member=document::PartDocument::from_serialized(*packet,&cache);
        if(refresh_save_properties(member,cache)) {
            packet=std::make_shared<const nlohmann::json>(member.serialized(cache));changed=true;
        }
    }
    return apply_physical_relation_parameters(doc,document::physical_values(doc,boundaries))||changed;
}

bool refresh_save_properties(assembly::AssemblyDocument& doc,const Workspace& context,
    const std::filesystem::path& owner_file,std::map<std::string,assembly::AssemblyDocument>* family_analysis) {
    kernel::OcctKernel kernel;
    std::set<std::string> visiting;
    std::map<std::pair<std::string,std::filesystem::path>,assembly::AssemblyDocument> completed;
    // Keep the original shared snapshot alive alongside the result: a raw
    // address alone could be reused after a closed-source candidate is freed.
    std::map<const kernel::BodyResult*,std::pair<kernel::BodySnapshot,kernel::BodySnapshot>> analyzed;
    const auto refresh=[&](const auto& self,assembly::AssemblyDocument& current,
        const std::filesystem::path& file)->bool {
        if(!visiting.insert(current.document_id).second)
            throw std::runtime_error("Cyclic Assembly source dependency");
        bool changed=false;
        for(auto& component:current.components) {
            if(component.source_missing||assembly::is_skeleton(component))continue;
            auto source_path=component.source_path;
            if(source_path.is_relative())source_path=file.parent_path()/source_path;
            if(const auto* part=context.open_part(component.source_document_id)) {
                const auto density=document::material_density_kg_mm3(part->session.document());
                if(component.density_kg_mm3!=density){component.density_kg_mm3=density;changed=true;}
            }
            const bool nested_source=component.source_kind==assembly::ComponentSourceKind::Assembly&&
                !current.owns_component_result(component.occurrence_id);
            const auto key=std::pair{component.source_document_id,source_path};
            if(nested_source&&!completed.contains(key)) {
                const auto* open=context.open_assembly(component.source_document_id);
                auto nested=open?open->session.document():read_family_assembly(&context,source_path,component.source_document_id,false);
                self(self,nested,open?open->path:source_path);
                completed.emplace(key,std::move(nested));
            }
            if(!component.calculated_source->physical_properties_current&&nested_source) {
                // An ordinary subassembly owns shared source snapshots, not a
                // duplicate B-Rep. Sum its analyzed sources without rebuilding
                // its mesh, solving placements or manufacturing a solid.
                const auto& nested=completed.at(key);
                const auto totals=assembly::physical_values(nested);
                if(totals.contains("model.volume")&&totals.contains("model.area")) {
                    auto body=component.calculated_source.get();
                    const double length=document::length_unit_mm(nested.document_units.at("Length"));
                    body.volume=totals.at("model.volume")*std::pow(length,3);
                    body.surface_area=totals.at("model.area")*length*length;
                    body.physical_properties_current=true;
                    component.calculated_source=std::move(body);changed=true;
                }
            } else if(!component.calculated_source->physical_properties_current) {
                const auto* identity=&component.calculated_source.get();
                if(const auto found=analyzed.find(identity);found!=analyzed.end())component.calculated_source=found->second.second;
                else {
                    auto body=component.calculated_source.get();kernel.ensure_body_properties(body);
                    auto snapshot=kernel::BodySnapshot{std::move(body)};
                    analyzed.emplace(identity,std::pair{component.calculated_source,snapshot});component.calculated_source=std::move(snapshot);
                }
                changed=true;
            }
            // Preserve Assembly-owned cut geometry and its pre-cut mass-volume
            // provenance. A heterogeneous cut cannot use a volume fraction.
            if(!nested_source)continue;
            auto captured=component;
            assembly::capture_nested_mass(captured,completed.at(key));
            if(captured.density_kg_mm3!=component.density_kg_mm3||captured.nested_mass_kg!=component.nested_mass_kg||
                captured.mass_volume_mm3!=component.mass_volume_mm3) {
                component.density_kg_mm3=captured.density_kg_mm3;
                component.nested_mass_kg=captured.nested_mass_kg;
                component.mass_volume_mm3=captured.mass_volume_mm3;changed=true;
            }
        }
        visiting.erase(current.document_id);
        return apply_physical_relation_parameters(current,assembly::physical_values(current))||changed;
    };
    bool changed=refresh(refresh,doc,owner_file);
    for(auto& [row,packet]:doc.family.evaluated) {
        auto member=assembly::AssemblyDocument::from_serialized(*packet);
        if(!share_family_source_analysis(member,doc))member.hydrate_sources(owner_file,native_source_resolver(context));
        static_cast<void>(refresh(refresh,member,owner_file));
        auto saved=member.serialized();
        if(saved!=*packet) {
            packet=std::make_shared<const nlohmann::json>(std::move(saved));changed=true;
        }
        if(family_analysis)family_analysis->emplace(row,std::move(member));
    }
    return changed;
}

namespace {
bool pending(const document::PartDocument& doc,const std::vector<kernel::BodyResult>& cache) {
    if(!cache.empty()&&!cache.back().physical_properties_current)return true;
    if(physical_relation_parameters_pending(doc,document::physical_values(doc,cache)))return true;
    for(const auto& [row,packet]:doc.family.evaluated) {
        std::vector<kernel::BodyResult> boundaries;
        const auto member=document::PartDocument::from_serialized(*packet,&boundaries);
        if(pending(member,boundaries))return true;
    }
    return false;
}
bool pending(const assembly::AssemblyDocument& doc,const Workspace& context,const std::filesystem::path& file) {
    for(const auto& component:doc.components)
        if(!component.source_missing&&!assembly::is_skeleton(component)&&!component.calculated_source->physical_properties_current)return true;
    if(physical_relation_parameters_pending(doc,assembly::physical_values(doc)))return true;
    for(const auto& [row,packet]:doc.family.evaluated) {
        auto member=assembly::AssemblyDocument::from_serialized(*packet);
        if(!share_family_source_analysis(member,doc))member.hydrate_sources(file,native_source_resolver(context));
        if(pending(member,context,file))return true;
    }
    return false;
}
}
bool save_properties_pending(const PartState& part) {return pending(part.session.document(),part.session.calculated_boundaries());}
bool save_properties_pending(const AssemblyState& assembly,const Workspace& context) {return pending(assembly.session.document(),context,assembly.path);}
} // namespace zima::workspace
