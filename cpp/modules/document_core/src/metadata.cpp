#include <zima/document/metadata.hpp>
#include <zima/document/precision.hpp>
#include <algorithm>
#include <cctype>
#include <set>
#include <charconv>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
namespace zima::document {
namespace {
std::string trim_parameter_line(std::string value) {
    const auto first=value.find_first_not_of(" \t\r");
    if(first==std::string::npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
void key(const std::string& value) {
    if(value.empty() || value.size()>256 || std::isspace(static_cast<unsigned char>(value.front())) ||
        std::isspace(static_cast<unsigned char>(value.back())) || std::ranges::any_of(value,[](unsigned char c){return c<32 || c==127;}))
        throw std::invalid_argument("Parameter keys must be nonempty, unique and contain no surrounding whitespace or control characters.");
    if(value.find_first_of("\\,=[]")!=std::string::npos || value.front()=='#' || value.front()==';')
        throw std::invalid_argument("Metadata keys contain a reserved native file separator.");
}
}
UserParameterData read_native_user_parameters(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open document: "+path.string());
    std::map<std::string,std::map<std::string,std::string>> sections;
    std::string section,line;bool line_started=false,keep=false;
    const auto consume=[&] {
        auto text=trim_parameter_line(std::move(line));line.clear();line_started=false;
        if(text.empty()||text.front()=='#'||text.front()==';')return;
        if(text.front()=='['&&text.back()==']'){section=trim_parameter_line(text.substr(1,text.size()-2));return;}
        if(section!="Document"&&section!="UserParameters"&&section!="UserParameterLabels"&&section!="UserParameterValues")return;
        const auto separator=text.find('=');if(separator==std::string::npos)throw std::runtime_error("Invalid INI document line");
        const auto key=trim_parameter_line(text.substr(0,separator));
        if(section=="Document"&&key!="format_version"&&key!="type")return;
        sections[section][key]=trim_parameter_line(text.substr(separator+1));
    };
    std::array<char,65536> buffer;
    while(input){
        input.read(buffer.data(),buffer.size());const auto count=static_cast<std::size_t>(input.gcount());
        for(std::size_t offset=0;offset<count;){
            const auto* start=buffer.data()+offset;const auto* end=static_cast<const char*>(std::memchr(start,'\n',count-offset));
            const auto length=end?static_cast<std::size_t>(end-start):count-offset;
            if(!line_started){
                const auto first=std::string_view(start,length).find_first_not_of(" \t\r");
                if(first!=std::string_view::npos){line_started=true;keep=start[first]=='['||section=="Document"||section=="UserParameters"||section=="UserParameterLabels"||section=="UserParameterValues";}
            }
            if(keep){
                // Document/cache lines can contain hundreds of MB. Only their
                // short header keys are inspected; parameter values stay exact.
                const bool bounded=section=="Document"||(!line.empty()&&line.front()=='[')||!line_started;
                if(bounded){if(line.size()<4096)line.append(start,std::min(length,4096-line.size()));}
                else line.append(start,length);
            }
            offset+=length+(end?1:0);if(end){if(keep)consume();else{line.clear();line_started=false;}keep=false;}
        }
    }
    if(!input.eof())throw std::runtime_error("Cannot read document: "+path.string());
    if(keep&&!line.empty())consume();
    const auto& header=sections["Document"];
    const auto type=header.find("type"),version=header.find("format_version");
    if(version==header.end()||type==header.end()||
        (type->second=="assembly"?version->second!="36":type->second!="part"||version->second!="46"))
        throw std::runtime_error(type!=header.end()&&type->second=="assembly"?"Unsupported ZIMA-CAD Assembly document format":"Unsupported ZIMA-CAD Part document format");
    UserParameterData result;
    std::stringstream order(sections["UserParameters"]["Order"]);std::string name;
    while(std::getline(order,name,',')){name=trim_parameter_line(std::move(name));if(!name.empty())result.order.push_back(name);}
    for(const auto& [key,value]:sections["UserParameterLabels"]){const auto separator=key.rfind('\\');result.labels[separator==std::string::npos?key:key.substr(0,separator)][separator==std::string::npos?std::string{}:key.substr(separator+1)]=value;}
    const auto values=sections["UserParameterValues"].find("Data");
    if(values!=sections["UserParameterValues"].end())result.values=nlohmann::json::parse(values->second).get<decltype(result.values)>();
    normalize_user_parameters(result);return result;
}
void validate_native_metadata_text(const std::string& text) {
    if(text.size()>65536 || text.find('\0')!=std::string::npos)
        throw std::invalid_argument("A parameter value is too long or contains a null character.");
    if(text.find_first_of("\r\n")!=std::string::npos || (!text.empty() &&
        (text.front()==' ' || text.front()=='\t' || text.back()==' ' || text.back()=='\t')))
        throw std::invalid_argument("Native metadata values must be single lines without surrounding spaces or tabs.");
}
void normalize_user_parameters(UserParameterData& data) {
    if(data.order.size()>4096)throw std::invalid_argument("A document supports at most 4096 user parameters.");
    std::set<std::string> seen;
    for(const auto& name:data.order) {
        key(name);if(!seen.insert(name).second)throw std::invalid_argument("Parameter keys must be nonempty, unique and contain no surrounding whitespace or control characters.");
    }
    for(const auto* localized:{&data.labels,&data.values})for(const auto& [name,values]:*localized) {
        if(!seen.contains(name))throw std::invalid_argument("Parameter labels and values must belong to an ordered parameter.");
        if(values.size()>128)throw std::invalid_argument("A parameter supports at most 128 language variants.");
        for(const auto& [language,text]:values) {
            if(!language.empty())key(language);
            if(localized==&data.labels)validate_native_metadata_text(text);
            else if(text.size()>1024*1024 || text.find('\0')!=std::string::npos)
                throw std::invalid_argument("A parameter value is too long or contains a null character.");
        }
    }
    data.flat.clear();
    for(const auto& name:data.order) {
        const auto found=data.values.find(name);
        if(found!=data.values.end())if(const auto shared=found->second.find("");shared!=found->second.end())data.flat[name]=shared->second;
    }
    if(const auto thickness=data.flat.find("SHEETMETAL_THICKNESS");thickness!=data.flat.end()&&!thickness->second.empty()) {
        double value{};const auto& text=thickness->second;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())
            throw std::invalid_argument("Sheet thickness must be a positive number in millimeters or unset.");
        validate_sheet_metal_defaults({value,0.5});
    }
    std::erase_if(data.labels,[](const auto& entry){return entry.second.empty();});
    std::erase_if(data.values,[](const auto& entry){return entry.second.empty();});
}
const std::map<std::string,std::vector<std::string>>& file_unit_choices() {
    static const std::map<std::string,std::vector<std::string>> choices{
        {"Length",{"mm","cm","m","in"}},{"Angle",{"deg","rad"}},{"Mass",{"kg","g","t","lb"}},
        {"Time",{"s","min"}},{"Temperature",{"C","K","F"}},{"Stress",{"Pa","kPa","MPa","GPa","psi"}}};
    return choices;
}
void validate_file_settings(const FileSettingsData& data) {
    if(data.sheet_metal)validate_sheet_metal_defaults(*data.sheet_metal);
    if(data.precision.contains("sheet_cut_tolerance"))static_cast<void>(sheet_cut_tolerance(data.precision));
    for(const auto& [name,choices]:file_unit_choices()) {
        const auto unit=data.units.find(name);
        if(unit==data.units.end() || std::ranges::find(choices,unit->second)==choices.end())
            throw std::invalid_argument("Choose a supported unit for every document quantity.");
    }
    for(const auto* name:{"linear_tolerance","angular_tolerance","mesh_deflection","decimal_places"}) {
        if(!data.precision.contains(name))throw std::invalid_argument("All document precision values are required.");
        const auto value=precision_value(data.precision,name,0);
        if(std::string_view(name)=="decimal_places") {
            if(value<0 || value>12 || value!=std::floor(value))throw std::invalid_argument("Decimal places must be an integer from 0 to 12.");
        } else if(value<0 || value>1000000 || (std::string_view(name)=="mesh_deflection" && value<1e-9))
            throw std::invalid_argument("Tolerances must be from 0 to 1000000; mesh deflection must be at least 0.000000001.");
    }
}
}
