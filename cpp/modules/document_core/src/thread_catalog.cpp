#include <zima/document/thread_catalog.hpp>
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace zima::document {
std::string_view bundled_thread_catalog(std::string_view standard);
namespace {
std::string_view trim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
std::optional<double> number(std::string_view source) {
    std::string text(trim(source));std::ranges::replace(text,',','.');
    double result{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
    if(error!=std::errc{} || end!=text.data()+text.size() || !std::isfinite(result))return {};
    return result;
}
std::vector<ThreadCatalogSize> read(std::string_view standard) {
    auto data=bundled_thread_catalog(standard);std::vector<ThreadCatalogSize> result;
    while(!data.empty()) {
        const auto end=data.find('\n');auto line=trim(data.substr(0,end));
        data=end==std::string_view::npos?std::string_view{}:data.substr(end+1);
        std::vector<std::string_view> fields;
        while(true) {const auto tab=line.find('\t');fields.push_back(trim(line.substr(0,tab)));
            if(tab==std::string_view::npos)break;line.remove_prefix(tab+1);}
        if(standard=="metric" && fields.size()>=6) {
            const auto d=number(fields[0]),pitch=number(fields[1]),d1=number(fields[3]),d3=number(fields[4]);
            if(!d||!pitch||!d1||!d3)continue;
            std::string name(fields[5]);std::erase(name,' ');
            for(std::size_t pos=0;(pos=name.find('x',pos))!=std::string::npos;pos+=2)name.replace(pos,1,"\xc3\x97");
            const bool preferred=name.find("\xc3\x97")==std::string::npos;
            result.push_back({std::move(name),*d,*pitch,*d1,*d3,preferred});
        } else if(standard=="whitworth" && fields.size()>=8) {
            const auto pitch=number(fields[2]),d=number(fields[3]),root=number(fields[7]);
            if(!pitch||!d||!root)continue;
            const auto name=fields[0];const bool preferred=name=="W 3/8"||name=="W 1/2"||name=="W 5/8"||name=="W 3/4"||name=="W 1";
            result.push_back({std::string(name),*d,*pitch,*root,*root,preferred});
        } else if(standard=="pipe" && fields.size()>=4) {
            const auto d=number(fields[1]),pitch_diameter=number(fields[2]),root=number(fields[3]);
            if(!d||!pitch_diameter||!root)continue;
            const auto name=fields[0];const bool preferred=name=="G 1/4"||name=="G 3/8"||name=="G 1/2"||name=="G 3/4"||name=="G 1";
            result.push_back({std::string(name),*d,(*d-*pitch_diameter)/.640327,*root,*root,preferred});
        }
    }
    return result;
}
}
const std::vector<ThreadCatalogSize>& thread_catalog(std::string_view standard) {
    if(standard=="metric") {static const auto values=read("metric");return values;}
    if(standard=="whitworth") {static const auto values=read("whitworth");return values;}
    if(standard=="pipe") {static const auto values=read("pipe");return values;}
    throw std::invalid_argument("Thread standard must be metric, whitworth or pipe.");
}
void select_opening_thread_size(HistoryContainer& value,ThreadStandard standard,
    const ThreadCatalogSize& size) {
    if(value.feature_kind!=FeatureKind::Thread)throw std::invalid_argument("This container is not an opening.");
    auto& p=value.thread;
    p.standard=standard;p.designation=size.designation;p.nominal_diameter=size.nominal_diameter;p.pitch=size.pitch;
    if(!p.custom_profile_diameter)p.profile_diameter=size.internal_root_diameter;
    if(p.end_condition_forward==EndCondition::Length && p.length_end_condition==EndCondition::Length)
        p.bore_length=std::max(p.bore_length,std::ceil((p.length_forward+p.runout_pitch_factor*p.pitch)*1000.0)/1000.0);
}
} // namespace zima::document
