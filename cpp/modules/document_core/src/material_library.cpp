#include <zima/document/material_library.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace zima::document {
namespace {
std::string trim(std::string text) {
    const auto first=text.find_first_not_of(" \t\r");if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r")-first+1);
}
}
MaterialData load_material_library(const std::filesystem::path& path) {
    auto extension=path.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".matz")throw std::invalid_argument("A native material library requires the .matz extension.");
    if(!std::filesystem::is_regular_file(path))throw std::invalid_argument("The material library must be an existing regular file.");
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open the material library.");
    const auto size=input.tellg();
    if(size<=0 || size>16*1024*1024)throw std::invalid_argument("The material library must contain 1 byte to 16 MiB.");
    std::string content(static_cast<std::size_t>(size),'\0');input.seekg(0);input.read(content.data(),static_cast<std::streamsize>(content.size()));
    if(input.gcount()!=static_cast<std::streamsize>(content.size()) || input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("The material library changed or could not be read completely.");
    if(content.starts_with("\xef\xbb\xbf"))content.erase(0,3);
    if(content.find('\0')!=std::string::npos)throw std::invalid_argument("The material library must be valid UTF-8 text without null characters.");
    try { static_cast<void>(nlohmann::json(content).dump()); }
    catch(const nlohmann::json::exception&){throw std::invalid_argument("The material library must be valid UTF-8 text without null characters.");}
    std::map<std::string,std::map<std::string,std::string>> sections;
    std::istringstream lines(content);std::string line,section;
    while(std::getline(lines,line)) {
        line=trim(std::move(line));if(line.empty() || line.front()=='#' || line.front()==';')continue;
        if(line.front()=='[' && line.back()==']') {
            section=trim(line.substr(1,line.size()-2));
            if(section!="Material" && section!="Properties" && section!="PropertyUnits" && section!="ParameterDescriptions")throw std::invalid_argument("Unknown section in the native material library.");
            if(!sections.try_emplace(section).second)throw std::invalid_argument("Duplicate section or key in the native material library.");
            continue;
        }
        const auto separator=line.find('=');
        if(section.empty() || separator==std::string::npos)throw std::invalid_argument("Invalid native material library line.");
        const auto key=trim(line.substr(0,separator));
        if(key.empty())throw std::invalid_argument("Invalid native material library line.");
        if(!sections.at(section).emplace(key,trim(line.substr(separator+1))).second)throw std::invalid_argument("Duplicate section or key in the native material library.");
    }
    if(!sections.contains("Material") || !sections.contains("Properties") || sections.at("Material").size()!=1 || !sections.at("Material").contains("Name") || sections.at("Material").at("Name").empty())throw std::invalid_argument("The material library requires a name and a Properties section.");
    MaterialData result;result.properties=std::move(sections.at("Properties"));
    if(!result.properties.emplace("MATERIAL_NAME",sections.at("Material").at("Name")).second)throw std::invalid_argument("The material name must be declared only in the Material section.");
    if(sections.contains("PropertyUnits"))result.units=std::move(sections.at("PropertyUnits"));
    if(sections.contains("ParameterDescriptions"))for(const auto& [key,value]:sections.at("ParameterDescriptions")) {
        const auto separator=key.rfind('\\');
        const auto property=separator==std::string::npos?key:key.substr(0,separator),language=separator==std::string::npos?std::string{}:key.substr(separator+1);
        if(!result.descriptions[property].emplace(language,value).second)throw std::invalid_argument("Duplicate section or key in the native material library.");
    }
    validate_material(result);return result;
}
}
