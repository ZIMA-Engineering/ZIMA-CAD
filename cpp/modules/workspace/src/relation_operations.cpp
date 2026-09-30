#include <zima/workspace/relation_operations.hpp>
#include <zima/document/precision.hpp>
#include <zima/document/component_source.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace zima::workspace {
namespace {
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
template<class Doc> void dimensions(Doc& doc,const std::map<std::string,double>& physical) {
    if(doc.relations.empty())return;
    const auto definitions=relation_dimensions(doc);
    const auto calculated=document::RelationProgram(doc.relations).evaluate(inputs(doc,physical),static_cast<int>(document::precision_value(doc.document_precision,"decimal_places",3)),true);
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
    const auto calculated=document::RelationProgram(doc.relations).evaluate(inputs(doc,physical),decimals);
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
void apply_relation_dimensions(document::PartDocument& d,const std::map<std::string,double>& p){dimensions(d,p);}
void apply_relation_dimensions(assembly::AssemblyDocument& d,const std::map<std::string,double>& p){dimensions(d,p);}
void apply_relation_parameters(document::PartDocument& d,const std::map<std::string,double>& p){parameters(d,p);}
void apply_relation_parameters(assembly::AssemblyDocument& d,const std::map<std::string,double>& p){parameters(d,p);}
}
