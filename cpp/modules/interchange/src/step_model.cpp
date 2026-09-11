#include <zima/interchange/step_model.hpp>
#include <zima/interchange/step.hpp>
#include <zima/document/precision.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <iomanip>
#include <stdexcept>
namespace zima::interchange {
namespace {
void insert_body(document::PartDocument& doc, document::HistoryContainer container,
        const StepPart& node, bool global) {
    auto graph=doc.body_history;
    if(graph.bodies().empty()&&!doc.history.empty()) {
        static_cast<void>(graph.create_body(doc.name));
        for(const auto& entry:doc.history_order)graph.insert(entry);
        if(doc.history_order.empty())for(const auto& old:doc.history)
            graph.insert({document::PartHistoryKind::Feature,old.id});
    }
    const auto id=graph.create_body(node.name);
    graph.insert({document::PartHistoryKind::Feature,container.id});
    if(global) {
        auto body=*graph.find(id);
        body.scope.placement={node.global_x,node.global_y,node.global_z,
            node.global_rotation_x,node.global_rotation_y,node.global_rotation_z};
        body.scope.placement.absolute_rotation_x=node.global_rotation_x;
        body.scope.placement.absolute_rotation_y=node.global_rotation_y;
        body.scope.placement.absolute_rotation_z=node.global_rotation_z;
        graph.update_body(std::move(body));
    }
    doc.history.push_back(std::move(container));doc.set_body_history(std::move(graph));
}
std::string shape_key(const kernel::BodyResult& body) {
    return std::to_string(std::hash<std::string>{}(body.kernel_shape));
}
void identify_assembly(kernel::StepProduct& product) {
    std::ostringstream identity;identity<<product.definition_id<<std::setprecision(17);
    for(const auto& child:product.children)identity<<'|'<<child.definition_id<<':'
        <<child.translation.x<<','<<child.translation.y<<','<<child.translation.z<<','
        <<child.rotation_degrees.x<<','<<child.rotation_degrees.y<<','<<child.rotation_degrees.z;
    product.definition_id=identity.str();
}
std::optional<kernel::StepProduct> snapshot_product(const assembly::OccurrenceSnapshot& node,
        const kernel::BodyResult& body) {
    if(!node.visible||node.manually_suppressed||node.dependency_suppressed)return std::nullopt;
    kernel::StepProduct product;product.name=node.name;product.definition_id=node.source_document_id;
    product.translation={node.placement.x,node.placement.y,node.placement.z};
    product.rotation_degrees={node.placement.rotation_x,node.placement.rotation_y,node.placement.rotation_z};
    if(node.source_kind==assembly::ComponentSourceKind::Assembly) {
        for(const auto& child:node.children) {
            if(!child.visible||child.manually_suppressed||child.dependency_suppressed)continue;
            const auto found=body.body_outputs.find(child.occurrence_id);
            if(found==body.body_outputs.end())throw std::runtime_error(
                "Podsestava nemá uloženou geometrii komponent pro STEP. Proveďte Regenerovat.");
            if(auto value=snapshot_product(child,found->second))product.children.push_back(std::move(*value));
        }
        if(product.children.empty())return std::nullopt;
        identify_assembly(product);
    } else {
        if(body.kernel_shape.empty())throw std::runtime_error("STEP díl nemá vypočtené těleso. Proveďte Regenerovat.");
        product.body=body;product.definition_id+=':'+shape_key(body);
    }
    return product;
}
}
StepImportedPart import_step_part(document::PartDocument doc,
        const std::vector<kernel::BodyResult>& previous,const std::filesystem::path& source) {
    const auto absolute=std::filesystem::absolute(source);
    const auto nodes=inspect_step_parts(absolute);
    std::vector<document::HistoryContainer> containers;
    std::vector<kernel::StepRequest> requests;
    for(const auto& node:nodes)if(!node.assembly) {
        auto container=document::PartDocument::create_imported_step_container(absolute,node.definition_id,node.name);
        requests.push_back({absolute.string(),node.definition_id,{},{},container.id});
        containers.push_back(std::move(container));
    }
    if(requests.empty())throw std::runtime_error("STEP neobsahuje žádné díly");
    kernel::OcctKernel kernel;
    auto frozen=kernel.import_step_components(requests,document::precision_value(doc.document_precision,"mesh_deflection",0.1));
    std::size_t index=0;
    for(const auto& node:nodes)if(!node.assembly) {
        auto container=std::move(containers.at(index));
        container.imported_step.frozen_brep=std::make_shared<const std::string>(frozen.at(index).kernel_shape);
        container.imported_step.topology=std::move(frozen.at(index).imported_step_topology);
        insert_body(doc,std::move(container),node,true);++index;
    }
    auto calculated=kernel.evaluate_history_incremental(doc.kernel_operations(),previous);
    return {std::move(doc),std::move(calculated),{}};
}
StepAssemblyImport import_step_assembly(const std::filesystem::path& source,
        const std::filesystem::path& directory,const std::map<std::string,std::string>& precision) {
    const auto absolute=std::filesystem::absolute(source);
    const auto nodes=inspect_step_parts(absolute);
    if(nodes.empty())throw std::runtime_error("STEP neobsahuje produktovou strukturu");
    StepAssemblyImport result;
    std::map<std::string,std::size_t> parts,assemblies;
    std::vector<const StepPart*> unique_parts;
    std::vector<document::HistoryContainer> containers;
    std::vector<kernel::StepRequest> requests;
    for(const auto& node:nodes)if(!node.assembly&&!parts.contains(node.definition_id)) {
        parts[node.definition_id]=unique_parts.size();unique_parts.push_back(&node);
        auto container=document::PartDocument::create_imported_step_container(absolute,node.definition_id,node.name);
        requests.push_back({absolute.string(),node.definition_id,{},{},container.id});containers.push_back(std::move(container));
    }
    kernel::OcctKernel kernel;
    auto frozen=kernel.import_step_components(requests,document::precision_value(precision,"mesh_deflection",0.1));
    for(std::size_t i=0;i<unique_parts.size();++i) {
        auto doc=document::PartDocument::create_default();doc.name=unique_parts[i]->definition_name;doc.document_precision=precision;
        auto container=std::move(containers[i]);
        container.imported_step.frozen_brep=std::make_shared<const std::string>(frozen[i].kernel_shape);
        container.imported_step.topology=std::move(frozen[i].imported_step_topology);
        insert_body(doc,std::move(container),*unique_parts[i],false);
        auto calculated=kernel.evaluate_history(doc.kernel_operations());
        result.parts.push_back({std::move(doc),std::move(calculated),directory/("part-"+std::to_string(i+1)+".prtz")});
    }
    std::map<std::string, kernel::BodySnapshot> source_snapshots;
    const auto assembled=[&](const StepImportedAssembly& generated,const std::string& name) {
        if (const auto found=source_snapshots.find(generated.document.document_id);
            found!=source_snapshots.end()) {
            auto value=assembly::AssemblyDocument::create_part_occurrence(name,
                generated.document.document_id,generated.path,found->second);
            value.source_kind=assembly::ComponentSourceKind::Assembly;
            value.nested_snapshot=generated.document.occurrence_snapshot();
            return value;
        }
        auto value=assembly::AssemblyDocument::create_assembly_occurrence(name,
            generated.document.document_id,generated.path,generated.document);
        source_snapshots.emplace(generated.document.document_id,value.calculated_source);
        return value;
    };
    std::set<std::string> visiting;
    std::function<assembly::PartOccurrence(const StepPart&)> occurrence;
    occurrence=[&](const StepPart& node) {
        assembly::PartOccurrence child;
        if(node.assembly) {
            if(visiting.contains(node.definition_id))throw std::runtime_error("Cyklická STEP sestava");
            if(!assemblies.contains(node.definition_id)) {
                visiting.insert(node.definition_id);
                auto doc=assembly::AssemblyDocument::create_default();doc.name=node.definition_name;doc.document_precision=precision;
                for(const auto& nested:nodes)if(nested.parent_path==node.component_path)doc.components.push_back(occurrence(nested));
                visiting.erase(node.definition_id);
                if(doc.components.empty())throw std::runtime_error("Prázdná STEP podsestava");
                const auto index=result.assemblies.size();assemblies[node.definition_id]=index;
                result.assemblies.push_back({std::move(doc),directory/("assembly-"+std::to_string(index+1)+".asmz")});
            }
            const auto& generated=result.assemblies.at(assemblies.at(node.definition_id));
            child=assembled(generated,node.name);
        } else {
            const auto& generated=result.parts.at(parts.at(node.definition_id));
            auto found=source_snapshots.find(generated.document.document_id);
            if(found==source_snapshots.end()) {
                auto snapshot=generated.calculated.back();
                snapshot.body_boundaries.clear();
                snapshot.body_inputs.clear();
                found=source_snapshots.emplace(generated.document.document_id,std::move(snapshot)).first;
            }
            child=assembly::AssemblyDocument::create_part_occurrence(node.name,generated.document.document_id,generated.path,found->second);
        }
        child.placement={node.x,node.y,node.z,node.rotation_x,node.rotation_y,node.rotation_z};return child;
    };
    std::vector<assembly::PartOccurrence> roots;
    for(const auto& node:nodes)if(node.parent_path.empty())roots.push_back(occurrence(node));
    // A single assembly at identity already is the STEP document root.
    if(roots.size()==1&&roots.front().source_kind==assembly::ComponentSourceKind::Assembly&&
            roots.front().placement==assembly::ComponentPlacement{}) {
        const auto root_id=roots.front().source_document_id;
        result.root_index=std::ranges::find_if(result.assemblies,[&](const auto& a){return a.document.document_id==root_id;})-result.assemblies.begin();
    } else {
        auto root=assembly::AssemblyDocument::create_default();root.name=source.stem().string();root.document_precision=precision;
        root.components=std::move(roots);result.root_index=result.assemblies.size();
        result.assemblies.push_back({std::move(root),directory/"step-root.asmz"});
    }
    if(result.parts.empty())throw std::runtime_error("STEP neobsahuje žádné díly");
    const auto& root=result.assemblies.at(result.root_index);
    result.root_occurrence=assembled(root,root.document.name);
    return result;
}
kernel::StepProduct step_product(const document::PartDocument& doc,const std::vector<kernel::BodyResult>& calculated) {
    if(calculated.empty())throw std::runtime_error("Part nemá vypočtené těleso. Proveďte Regenerovat.");
    kernel::StepProduct root;root.name=doc.name;root.definition_id=doc.document_id;
    const auto& output=calculated.back();
    if(doc.body_history.bodies().empty())root.body=output;
    else for(const auto& id:doc.body_history.available_before(doc.body_history.order().size())) {
        if(const auto* body=doc.body_history.find(id);body&&!body->visible)continue;
        if(const auto* boolean=doc.body_history.find_boolean(id);boolean&&!boolean->visible)continue;
        const auto found=output.body_outputs.find(id);if(found==output.body_outputs.end())continue;
        if(found->second->kernel_shape.empty())continue;
        kernel::StepProduct child;child.definition_id=id;child.body=found->second;
        if(const auto* body=doc.body_history.find(id))child.name=body->name;
        else {const auto& booleans=doc.body_history.booleans();const auto b=std::ranges::find(booleans,id,&document::BodyBoolean::id);child.name=b==booleans.end()?doc.name:b->name;}
        root.children.push_back(std::move(child));
    }
    if(root.children.empty()&&root.body.kernel_shape.empty())throw std::runtime_error("Part nemá viditelné těleso pro STEP");
    return root;
}
kernel::StepProduct step_product(const assembly::AssemblyDocument& doc) {
    kernel::StepProduct root;root.name=doc.name;root.definition_id=doc.document_id;
    for(const auto& node:doc.occurrence_snapshot()) {
        const auto* component=doc.find_occurrence(node.occurrence_id);
        if(auto product=snapshot_product(node,component->calculated_source))root.children.push_back(std::move(*product));
    }
    if(root.children.empty())throw std::runtime_error("Sestava nemá viditelné díly pro STEP");
    identify_assembly(root);return root;
}
}
