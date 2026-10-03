#include <zima/workspace/relation_operations.hpp>
#include <zima/document/precision.hpp>
#include <zima/document/component_source.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <limits>
#include <zima/document/physical_properties.hpp>

namespace zima::workspace {
namespace {
template<class Doc> double degrees_per_angle_unit(const Doc& doc) {
    return doc.document_units.at("Angle")=="rad"?180./std::numbers::pi:1.;
}
template<class Doc> document::RelationInputs inputs(const Doc& doc,const std::map<std::string,double>& physical) {
    document::RelationInputs result;
    for(const auto& [name,text]:doc.user_parameters) {
        double number{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
        document::RelationValue value{text};
        if(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&std::isfinite(number)){value.data=number;value.literal=true;}
        result[name]={value,true};
    }
    for(const auto& name:{"model.mass","model.area","model.volume","material.density"}) {
        const auto found=physical.find(name);
        document::RelationValue value{std::string{}};
        if(found!=physical.end())value.data=found->second;
        value.units=std::string(name)=="model.mass"?std::array<int,3>{0,0,1}:std::string(name)=="model.area"?std::array<int,3>{2,0,0}:std::string(name)=="model.volume"?std::array<int,3>{3,0,0}:std::array<int,3>{-3,0,1};
        result[name]={value,false,true};
    }
    for(const auto& [name,dimension]:relation_dimensions(doc))result[name]=dimension.input;
    if constexpr(requires{doc.body_color;})result["color"]={{doc.body_color},true};
    else result["color"]={{std::string{}},false};
    return result;
}
template<class Doc> void convert_units(Doc& doc,const std::map<std::string,std::string>& next) {
    const auto& old=doc.document_units;
    const auto angular=[](const std::string& unit){return unit=="rad"?180./std::numbers::pi:1.;};
    const std::array<double,3> factors{document::length_unit_mm(old.at("Length"))/document::length_unit_mm(next.at("Length")),
        angular(old.at("Angle"))/angular(next.at("Angle")),document::mass_unit_kg(old.at("Mass"))/document::mass_unit_kg(next.at("Mass"))};
    if(doc.relations.empty()||factors==std::array<double,3>{1,1,1})return;
    // Quantity inference needs types, not an OCCT calculation or physical values.
    const std::map<std::string,double> quantities{{"model.mass",0},{"model.area",0},{"model.volume",0},{"material.density",0}};
    // This reserved Part parameter is explicitly native millimetres, including
    // when Relations assign it. Its existing storage contract must not change.
    std::set<std::string> fixed_units;if constexpr(requires{doc.body_history;})fixed_units.insert("SHEETMETAL_THICKNESS");
    const auto conversion=document::RelationProgram(doc.relations).convert_units(inputs(doc,quantities),factors,fixed_units,degrees_per_angle_unit(doc));
    auto parameters=doc.user_parameters;auto localized=doc.user_parameter_values;
    for(const auto& [name,target]:conversion.outputs) {
        auto stored=parameters.find(name);if(stored==parameters.end())continue;
        const double factor=target.factor;if(factor==1)continue;
        const auto& text=stored->second;double number{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
        const double converted=number*factor;
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(converted)||(number!=0&&converted==0))
            throw document::RelationError(target.line,1,"Cannot safely convert relation units.",name);
        char buffer[64];const auto formatted=std::to_chars(buffer,buffer+sizeof(buffer),converted,std::chars_format::general,std::numeric_limits<double>::max_digits10);
        if(formatted.ec!=std::errc{})throw document::RelationError(target.line,1,"Cannot safely convert relation units.",name);
        stored->second=std::string(buffer,formatted.ptr);localized[name][""]=stored->second;
    }
    doc.relations=conversion.source;doc.user_parameters=std::move(parameters);doc.user_parameter_values=std::move(localized);
}
template<class Doc> void dimensions(Doc& doc,const std::map<std::string,double>& physical) {
    if(doc.relations.empty())return;
    const auto definitions=relation_dimensions(doc);
    const auto calculated=document::RelationProgram(doc.relations).evaluate(inputs(doc,physical),static_cast<int>(document::precision_value(doc.document_precision,"decimal_places",3)),true,degrees_per_angle_unit(doc));
    for(const auto& [name,value]:calculated)if(const auto d=definitions.find(name);d!=definitions.end()) {
        const double native=std::get<double>(value.data)*d->second.native_scale;
        const double previous=std::get<double>(d->second.input.value.data)*d->second.native_scale;
        if(native==previous&&std::signbit(native)==std::signbit(previous))continue;
        if(!assign_driving_dimension(doc,d->second.binding,native))throw std::invalid_argument("The relation dimension cannot be assigned.");
    }
}
template<class Doc> void parameters(Doc& doc,const std::map<std::string,double>& physical) {
    if(doc.relations.empty())return;
    const auto definitions=relation_dimensions(doc);
    const int decimals=static_cast<int>(document::precision_value(doc.document_precision,"decimal_places",3));
    const auto calculated=document::RelationProgram(doc.relations).evaluate(inputs(doc,physical),decimals,false,degrees_per_angle_unit(doc));
    for(const auto& [name,value]:calculated)if(!definitions.contains(name)) {
        if(name=="color") {
            const auto* color=std::get_if<std::string>(&value.data);
            if(!color||color->size()!=7||color->front()!='#'||
                !std::all_of(color->begin()+1,color->end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))
                throw std::invalid_argument("Color must be text in #RRGGBB format.");
            if constexpr(requires{doc.body_color;}) {
                auto appearance=document::component_appearance(doc);
                appearance.body.color=*color;
                for(auto& [id,style]:appearance.bodies)style.color=*color;
                for(auto& group:appearance.groups)group.style.color=*color;
                doc.appearance=std::move(appearance);doc.body_color=*color;
                for(auto& [face,shade]:doc.face_colors)shade=*color;
            }
            continue;
        }
        const auto text=document::relation_value_text(value,decimals);
        doc.user_parameters[name]=text;doc.user_parameter_values[name][""]=text;
        if(std::ranges::find(doc.user_parameter_order,name)==doc.user_parameter_order.end())doc.user_parameter_order.push_back(name);
    }
}
}
document::RelationInputs relation_inputs(const document::PartDocument& d,const std::map<std::string,double>& p){return inputs(d,p);}
document::RelationInputs relation_inputs(const assembly::AssemblyDocument& d,const std::map<std::string,double>& p){return inputs(d,p);}
void convert_relation_units(document::PartDocument& d,const std::map<std::string,std::string>& u){convert_units(d,u);}
void convert_relation_units(assembly::AssemblyDocument& d,const std::map<std::string,std::string>& u){convert_units(d,u);}
void apply_relation_dimensions(document::PartDocument& d,const std::map<std::string,double>& p){dimensions(d,p);}
void apply_relation_dimensions(assembly::AssemblyDocument& d,const std::map<std::string,double>& p){dimensions(d,p);}
void apply_relation_parameters(document::PartDocument& d,const std::map<std::string,double>& p){parameters(d,p);}
void apply_relation_parameters(assembly::AssemblyDocument& d,const std::map<std::string,double>& p){parameters(d,p);}
}
