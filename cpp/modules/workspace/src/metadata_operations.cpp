#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/precision.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <algorithm>
#include "metadata_transaction.hpp"
namespace zima::workspace {
namespace {
template<class Range> auto named(Range& values,const std::string& id) -> decltype(&values.front().name) {
    for(auto& value:values)if(value.id==id)return &value.name;
    return nullptr;
}
template<class Doc> auto object_name(Doc& doc,const std::string& kind,const std::string& id) -> decltype(&doc.name) {
    if(kind=="document"&&id==doc.document_id)return &doc.name;
    if constexpr(requires {doc.sheets;}) {
        if(kind=="drawing-sheet")return named(doc.sheets,id);
        if(kind=="drawing-view")for(auto& sheet:doc.sheets)if(auto* name=named(sheet.views,id))return name;
    } else {
        if(kind=="document-section")return named(doc.sections,id);
        if(kind=="document-measurement")return named(doc.measurements,id);
        if(kind=="part-construction"||kind=="assembly-construction")return named(doc.constructions,id);
        if(kind=="curve3d-point")for(auto& curve:doc.constructions)if(auto* name=named(curve.curve_points,id))return name;
        if(kind=="part-sketch"||kind=="assembly-sketch")return named(doc.sketches,id);
        if constexpr(requires {doc.history;}) {
            if(kind=="part-container")return named(doc.history,id);
            if(kind=="body-properties")return named(doc.body_properties,id);
        } else {
            if(kind=="assembly-sketch-container")return named(doc.sketch_containers,id);
            if(kind=="assembly-cut")for(auto& cut:doc.cuts)if(cut.definition.id==id)return &cut.definition.name;
            if(kind=="part-occurrence"||kind=="assembly-occurrence")for(auto& occurrence:doc.components)
                if(occurrence.occurrence_id==id)return &occurrence.name;
        }
    }
    return nullptr;
}
template<class Doc> std::optional<std::string> read_object_name(const Doc& doc,const std::string& kind,const std::string& id) {
    if constexpr(requires {doc.body_history;}) {
        if(kind=="part-body") {if(auto* body=doc.body_history.find(id))return body->name;}
        if(kind=="part-body-boolean") {if(auto* body=doc.body_history.find_boolean(id))return body->name;}
    }
    if(auto* name=object_name(doc,kind,id))return *name;
    return std::nullopt;
}
template<class Doc> void write_object_name(Doc& doc,const std::string& kind,const std::string& id,const std::string& name) {
    if constexpr(requires {doc.body_history;}) {
        if(kind=="part-body") {auto body=*doc.body_history.find(id);body.name=name;doc.body_history.update_body(std::move(body));return;}
        if(kind=="part-body-boolean") {auto body=*doc.body_history.find_boolean(id);body.name=name;doc.body_history.update_boolean(std::move(body));return;}
    }
    *object_name(doc,kind,id)=name;
}
template<class Doc> document::UserParameterData parameters(const Doc& doc) {
    document::UserParameterData data{doc.user_parameters,doc.user_parameter_order,doc.user_parameter_labels,doc.user_parameter_values};
    // Native factories may initialize shared parameters before a UI order exists.
    for(const auto& [name,value]:data.flat) {
        if(std::ranges::find(data.order,name)==data.order.end())data.order.push_back(name);
        data.values[name][""]=value;
    }
    document::normalize_user_parameters(data);return data;
}
}
std::optional<std::string> tree_object_name(const Workspace& live,const std::string& document,const std::string& kind,const std::string& id) {
    if(auto* part=live.open_part(document))return read_object_name(part->session.document(),kind,id);
    if(auto* assembly=live.open_assembly(document))return read_object_name(assembly->session.document(),kind,id);
    if(auto* drawing=live.open_drawing(document))return read_object_name(drawing->document(),kind,id);
    return std::nullopt;
}
bool rename_tree_object(Workspace& live,const std::string& document,const std::string& kind,const std::string& id,const std::string& name) {
    if(name.empty()||name.find_first_of("\r\n\t")!=std::string::npos||name.find_first_not_of(' ')==std::string::npos)
        throw std::invalid_argument("Enter a non-empty single-line name.");
    const auto before=tree_object_name(live,document,kind,id);
    if(!before)throw std::invalid_argument("This object has no editable name.");
    if(*before==name)return false;
    if(auto* part=live.open_part(document)) {
        auto next=part->session.document();write_object_name(next,kind,id,name);
        commit_part_document(live,document,std::move(next),part->session.calculated_boundaries());
    } else if(auto* assembly=live.open_assembly(document)) {
        auto next=assembly->session.document();write_object_name(next,kind,id,name);assembly->session.commit(std::move(next));
    } else if(auto* drawing=live.open_drawing(document)) {
        auto next=drawing->document();write_object_name(next,kind,id,name);drawing->commit(std::move(next));
    }
    return true;
}
document::UserParameterData user_parameters(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){return parameters(doc);});
}
document::FileSettingsData file_settings(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){
        document::FileSettingsData result{doc.document_units,doc.document_precision};
        if constexpr(std::is_same_v<std::decay_t<decltype(doc)>,document::PartDocument>)result.sheet_metal=document::sheet_metal_defaults(doc);
        return result;
    });
}
bool set_user_parameters(Workspace& live,const std::string& id,document::UserParameterData values) {
    document::normalize_user_parameters(values);
    return metadata_detail::write(live,id,[&](auto& doc) {
        doc.user_parameters=std::move(values.flat);doc.user_parameter_order=std::move(values.order);
        doc.user_parameter_labels=std::move(values.labels);doc.user_parameter_values=std::move(values.values);
    },[](const auto& a,const auto& b){return parameters(a)==parameters(b);});
}
SettingsChange set_file_settings(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,document::FileSettingsData values) {
    document::validate_file_settings(values);
    const auto before=file_settings(live,id);
    if(!values.sheet_metal)values.sheet_metal=before.sheet_metal;
    if(values.sheet_metal&&!before.sheet_metal)throw std::invalid_argument("Sheet metal defaults belong to a Part document.");
    if(before==values)return {};
    const bool precision_changed=document::precision_value(before.precision,"linear_tolerance",.001)!=document::precision_value(values.precision,"linear_tolerance",.001) ||
        document::precision_value(before.precision,"mesh_deflection",.1)!=document::precision_value(values.precision,"mesh_deflection",.1);
    if(auto* part=live.open_part(id)) {
        const bool cut_tolerance_changed=document::sheet_cut_tolerance(before.precision)!=document::sheet_cut_tolerance(values.precision);
        auto next=part->session.document();next.document_units=std::move(values.units);next.document_precision=std::move(values.precision);
        if(values.sheet_metal)document::set_sheet_metal_defaults(next,*values.sheet_metal);
        bool calculate=before.sheet_metal!=values.sheet_metal&&std::ranges::any_of(next.history,[](const auto& feature) {
            return feature.feature_kind==document::FeatureKind::Flat||feature.feature_kind==document::FeatureKind::Bend||
                (feature.feature_kind==document::FeatureKind::Revolution&&feature.revolution.sheet_metal);
        });
        if(precision_changed||cut_tolerance_changed) {
            const auto original=part->session.document().kernel_operations(false,true),requested=next.kernel_operations(false,true);
            calculate=calculate||kernel::history_fingerprint(original,original.size())!=kernel::history_fingerprint(requested,requested.size());
        }
        auto calculated=part->session.calculated_boundaries();
        if(calculate)calculated=calculate_part_with_resolved_references(kernel,next,&calculated,PartCalculationPolicy{true});
        document::refresh_physical_relations(next,document::physical_values(next,calculated));
        commit_part_document(live,id,std::move(next),std::move(calculated));return {true,calculate};
    }
    if(auto* assembly=live.open_assembly(id)) {
        auto next=assembly->session.document();next.document_units=std::move(values.units);next.document_precision=std::move(values.precision);
        const bool calculate=precision_changed && !next.cuts.empty();
        if(calculate)calculate_resolved_assembly_cuts(kernel,next);
        document::refresh_physical_relations(next,assembly::physical_values(next));
        assembly->session.commit(std::move(next));return {true,calculate};
    }
    throw std::invalid_argument("Document metadata requires an open Part or Assembly.");
}
}
