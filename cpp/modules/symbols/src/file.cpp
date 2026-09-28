#include <zima/symbols/native_document.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <algorithm>
#include <set>
namespace zima::symbols {
Definition Definition::load(const std::filesystem::path& path) {
    return native_definition(document::PartDocument::load(path));
}
void Definition::save(const std::filesystem::path& path) const {
    native_document(*this).save(path);
}
document::PartDocument native_document(const Definition& definition) {
    definition.validate();auto part=document::PartDocument::create_default();
    part.name=definition.name;part.symbol_editor_definition=definition.serialized();
    document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body(definition.name));
    document::FamilyTable table;
    for(auto sketch:definition.sketches) {
        auto container=document::PartDocument::create_sketch_container();container.name=sketch.name;
        container.suppressed=std::ranges::find(definition.variants.at(definition.default_variant).sketches,sketch.id)==definition.variants.at(definition.default_variant).sketches.end();
        sketch.owner_container_id=container.id;
        bodies.insert({document::PartHistoryKind::Feature,container.id});
        table.columns.push_back(sketch.id);table.bindings[sketch.id]={"feature",container.id,{}};
        part.history.push_back(std::move(container));part.sketches.push_back(std::move(sketch));
    }
    for(const auto& [key,variant]:definition.variants) {
        document::FamilyInstance row;row.id=key;row.name=key;
        for(const auto& sketch:definition.sketches)row.values[sketch.id]=std::ranges::find(variant.sketches,sketch.id)!=variant.sketches.end()?"yes":"no";
        table.instances.push_back(std::move(row));
    }
    part.set_body_history(std::move(bodies));part.family_table=document::serialize_family_table(table);
    part.validate_body_ownership();return part;
}
Definition native_definition(const document::PartDocument& part, bool insertion_snapshot) {
    if(!part.symbol_editor_definition)throw std::invalid_argument("This Part is not a symbol library document");
    auto definition=Definition::from_serialized(*part.symbol_editor_definition);
    definition.sketches=part.sketches;
    for(auto& sketch:definition.sketches)sketch.owner_container_id.clear();
    const auto table=document::parse_family_table(part.family_table);
    auto variants=definition.variants;definition.variants.clear();
    for(const auto& row:table.instances) {
        auto variant=variants.contains(row.id)?variants.at(row.id):Variant{};
        const auto original_order=variant.sketches;variant.sketches.clear();
        for(const auto& sketch:part.sketches) {
            const auto feature=std::ranges::find(part.history,sketch.owner_container_id,&document::HistoryContainer::id);
            if(feature==part.history.end())continue;
            bool present=!feature->suppressed;
            for(const auto& [column,binding]:table.bindings)if(binding.kind=="feature"&&binding.owner_id==feature->id) {
                if(const auto found=row.values.find(column);found!=row.values.end()&&!found->second.empty())present=found->second=="yes";
            }
            const auto* body=part.body_owner_for_object(feature->id);
            if(!body)present=false;
            else if(insertion_snapshot) {
                bool body_present=!body->suppressed;
                for(const auto& [column,binding]:table.bindings)if(binding.kind=="body"&&binding.owner_id==body->scope.id)
                    if(const auto found=row.values.find(column);found!=row.values.end()&&!found->second.empty())body_present=found->second=="yes";
                const auto entry=std::ranges::find(body->entries,feature->id,&document::PartHistoryEntry::id);
                if(!body_present||!body->visible||entry==body->entries.end()||static_cast<std::size_t>(entry-body->entries.begin())>=body->cursor)present=false;
            }
            if(present)variant.sketches.push_back(sketch.id);
        }
        auto ordered=original_order;
        std::erase_if(ordered,[&](const auto& id){return std::ranges::find(variant.sketches,id)==variant.sketches.end();});
        for(const auto& id:variant.sketches)if(std::ranges::find(ordered,id)==ordered.end())ordered.push_back(id);
        variant.sketches=std::move(ordered);definition.variants.emplace(row.id,std::move(variant));
    }
    if(definition.variants.empty())throw std::invalid_argument("A symbol requires a Family variant");
    if(!definition.variants.contains(definition.default_variant))definition.default_variant=definition.variants.begin()->first;
    std::erase_if(definition.fields,[&](const auto& entry){const auto& field=entry.second;
        const auto sketch=std::ranges::find(definition.sketches,field.sketch_id,&sketcher::Sketch::id);
        return sketch==definition.sketches.end()||std::ranges::find(sketch->texts,field.text_id,&sketcher::SketchText::id)==sketch->texts.end();});
    for(auto& [key,row]:definition.variants) {
        std::erase_if(row.text_values,[&](const auto& item){return !definition.fields.contains(item.first);});
        std::erase_if(row.hidden_texts,[&](const auto& field){return !definition.fields.contains(field);});
    }
    std::erase_if(definition.pens,[&](auto& entry){return std::ranges::find(definition.sketches,entry.first,&sketcher::Sketch::id)==definition.sketches.end();});
    for(auto& [id,pens]:definition.pens) {
        const auto& sketch=*std::ranges::find(definition.sketches,id,&sketcher::Sketch::id);std::set<std::string> curves;
        const auto add=[&](const auto& values){for(const auto& item:values)curves.insert(item.id);};
        add(sketch.segments);add(sketch.circles);add(sketch.arcs);add(sketch.ellipses);add(sketch.elliptical_arcs);add(sketch.bsplines);
        std::erase_if(pens,[&](const auto& item){return !curves.contains(item.first);});
    }
    definition.validate();return definition;
}
}
