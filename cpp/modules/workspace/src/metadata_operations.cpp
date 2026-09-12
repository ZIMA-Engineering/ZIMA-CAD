#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/precision.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <algorithm>
#include "metadata_transaction.hpp"
namespace zima::workspace {
namespace {
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
document::UserParameterData user_parameters(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){return parameters(doc);});
}
document::FileSettingsData file_settings(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){return document::FileSettingsData{doc.document_units,doc.document_precision};});
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
    if(before==values)return {};
    const bool precision_changed=document::precision_value(before.precision,"linear_tolerance",.001)!=document::precision_value(values.precision,"linear_tolerance",.001) ||
        document::precision_value(before.precision,"mesh_deflection",.1)!=document::precision_value(values.precision,"mesh_deflection",.1);
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();next.document_units=std::move(values.units);next.document_precision=std::move(values.precision);
        bool calculate=false;
        if(precision_changed) {
            const auto original=part->session.document().kernel_operations(false,true),requested=next.kernel_operations(false,true);
            calculate=kernel::history_fingerprint(original,original.size())!=kernel::history_fingerprint(requested,requested.size());
        }
        auto calculated=part->session.calculated_boundaries();
        if(calculate)calculated=calculate_part_with_resolved_references(kernel,next,&calculated,PartCalculationPolicy{true});
        document::refresh_physical_relations(next,document::physical_values(next,calculated));
        part->session.commit(std::move(next),std::move(calculated));return {true,calculate};
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
