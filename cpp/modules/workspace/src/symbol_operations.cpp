#include <zima/workspace/symbol_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/document/file_path.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <algorithm>
#include <cctype>
namespace zima::workspace {
namespace {
std::filesystem::path checked_path(const std::filesystem::path& source) {
    auto path=std::filesystem::absolute(source).lexically_normal();
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return std::tolower(c);});
    if(ext!=".symz")throw std::invalid_argument("A symbol document requires the symz extension");
    return path;
}
std::string insert(Workspace& live,symbols::Definition definition,const std::filesystem::path& path) {
    definition.validate();auto part=document::PartDocument::create_default();part.name=definition.name;
    for(auto sketch:definition.sketches) {
        auto container=document::PartDocument::create_sketch_container();container.name=sketch.name;
        sketch.owner_container_id=container.id;
        for(auto& text:sketch.texts)sketcher::rebuild_text_contours(text,true);
        part.insert_history_entry(document::PartHistoryKind::Feature,container.id);
        part.history.push_back(std::move(container));part.sketches.push_back(std::move(sketch));
    }
    const auto id=part.document_id;live.add_part(std::move(part),{},path);
    live.open_part(id)->symbol_definition=definition.serialized();return id;
}
}
bool is_symbol_document(const Workspace& live,const std::string& id) {
    const auto* part=live.open_part(id);return part&&part->symbol_definition.has_value();
}
std::string open_symbol_document(Workspace& live,const std::filesystem::path& source) {
    const auto path=checked_path(source);
    if(const auto id=live.document_id_for_path(path)) {
        if(!is_symbol_document(live,*id))throw std::invalid_argument("The path is used by a different document type");
        return *id;
    }
    return insert(live,symbols::Definition::load(path),path);
}
std::string create_symbol_document(Workspace& live,const std::string& name,const std::filesystem::path& target) {
    const auto path=checked_path(target);
    if(name.empty()||std::filesystem::exists(path)||live.document_id_for_path(path))throw std::invalid_argument("Invalid or occupied symbol document path");
    symbols::Definition d;d.id=kernel::make_stable_id();d.name=name;
    d.sketches.push_back(sketcher::Sketch::create_default());d.default_variant="default";
    d.variants["default"].sketches={d.sketches.front().id};return insert(live,std::move(d),path);
}
symbols::Definition edited_symbol_definition(const Workspace& live,const std::string& id) {
    const auto* part=live.open_part(id);
    if(!part||!part->symbol_definition)throw std::invalid_argument("This document is not a symbol");
    auto d=symbols::Definition::from_serialized(*part->symbol_definition);
    d.sketches=part->session.document().sketches;
    for(auto& sketch:d.sketches)sketch.owner_container_id.clear();
    // Removing a text or curve also removes its associated field/pen metadata.
    std::erase_if(d.fields,[&](const auto& entry){const auto& f=entry.second;
        const auto s=std::ranges::find(d.sketches,f.sketch_id,&sketcher::Sketch::id);
        return s==d.sketches.end()||std::ranges::find(s->texts,f.text_id,&sketcher::SketchText::id)==s->texts.end();});
    for(auto& [key,row]:d.variants) {
        std::erase_if(row.text_values,[&](const auto& v){return !d.fields.contains(v.first);});
        std::erase_if(row.hidden_texts,[&](const auto& v){return !d.fields.contains(v);});
    }
    for(auto& [id,pens]:d.pens) {
        const auto s=std::ranges::find(d.sketches,id,&sketcher::Sketch::id);std::set<std::string> curves;
        if(s!=d.sketches.end()){const auto add=[&](const auto& rows){for(const auto& r:rows)curves.insert(r.id);};
            add(s->segments);add(s->circles);add(s->arcs);add(s->ellipses);add(s->elliptical_arcs);add(s->bsplines);}
        std::erase_if(pens,[&](const auto& p){return !curves.contains(p.first);});
    }
    d.validate();return d;
}
void save_symbol_document(Workspace& live,const std::string& id,const std::filesystem::path& target,bool copy) {
    auto definition=edited_symbol_definition(live,id);const auto path=checked_path(target);
    auto* part=live.open_part(id);
    if(const auto owner=live.document_id_for_path(path);owner&&(*owner!=id||copy))throw std::invalid_argument("The target symbol is already open");
    live.reserve_file(path);definition.save(path);
    if(!copy){part->path=path;part->session.mark_saved();}
}
const std::vector<symbols::Placement>& symbol_annotations(const Workspace& live,const std::string& id,const std::string& sheet) {
    if(const auto* part=live.open_part(id)) {
        if(part->symbol_definition||!sheet.empty())throw std::invalid_argument("Invalid symbol annotation owner");
        return part->session.document().symbol_annotations;
    }
    if(const auto* assembly=live.open_assembly(id)) {
        if(!sheet.empty())throw std::invalid_argument("A model annotation cannot belong to a Drawing sheet");
        return assembly->session.document().symbol_annotations;
    }
    if(const auto* drawing=live.open_drawing(id))if(const auto* target=drawing->document().find_sheet(sheet))return target->symbol_annotations;
    throw std::invalid_argument("Symbol annotation document or sheet does not exist");
}
namespace {
bool commit_annotations(Workspace& live,const std::string& id,const std::string& sheet,std::vector<symbols::Placement> values) {
    if(id!=live.active_document_id())throw std::invalid_argument("Activate the annotation document before editing");
    if(values==symbol_annotations(live,id,sheet))return false;
    static_cast<void>(symbols::placements_json(values));
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();next.symbol_annotations=std::move(values);
        part->session.commit(std::move(next),part->session.calculated_boundaries());
    } else if(auto* assembly=live.open_assembly(id)) {
        auto next=assembly->session.document();next.symbol_annotations=std::move(values);assembly->session.commit(std::move(next));
    } else {
        auto* drawing=live.open_drawing(id);auto next=drawing->document();
        auto* target=next.find_sheet(sheet);target->symbol_annotations=std::move(values);
        std::erase_if(target->symbol_contacts,[&](const auto& entry){return std::ranges::none_of(target->symbol_annotations,[&](const auto& p){return p.symbol.id==entry.first&&p.reference;});});
        drawing->commit(std::move(next));
    }
    return true;
}
}
bool store_symbol_annotation(Workspace& live,const std::string& id,const symbols::Placement& value,const std::string& sheet) {
    value.validate();auto values=symbol_annotations(live,id,sheet);
    const auto found=std::ranges::find_if(values,[&](const auto& item){return item.symbol.id==value.symbol.id;});
    if(found==values.end())values.push_back(value);else *found=value;
    return commit_annotations(live,id,sheet,std::move(values));
}
bool remove_symbol_annotation(Workspace& live,const std::string& id,const std::string& symbol,const std::string& sheet) {
    auto values=symbol_annotations(live,id,sheet);
    std::erase_if(values,[&](const auto& item){return item.symbol.id==symbol;});
    return commit_annotations(live,id,sheet,std::move(values));
}
}
