#include "settings.hpp"
#include "../common/document_naming.hpp"
#include <zima/document/precision.hpp>
#include "../common/installation.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace zima::cli {
namespace {
namespace fs=std::filesystem;
using Values=std::map<std::string,std::string>;
std::string trim(std::string text){
    const auto start=text.find_first_not_of(" \t\r\n");
    return start==std::string::npos?std::string{}:text.substr(start,text.find_last_not_of(" \t\r\n")-start+1);
}
std::string value_text(const std::string& text){
    std::string result;bool quoted=false;
    for(std::size_t i=0;i<text.size();++i){
        const char c=text[i];
        if(c=='"'){quoted=!quoted;continue;}
        if(c==';'&&!quoted)break;
        if(c!='\\'){result+=c;continue;}
        if(++i==text.size())throw std::runtime_error("Incomplete INI escape");
        switch(text[i]){
            case '\\':result+='\\';break;case '"':result+='"';break;
            case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
            default:throw std::runtime_error("Unsupported INI escape; use forward slashes or escaped backslashes in paths");
        }
    }
    if(quoted)throw std::runtime_error("Unclosed INI quote");
    result=trim(result);
    if(result.starts_with("@@"))result.erase(0,1);
    else if(result.starts_with('@'))throw std::runtime_error("CLI config values must be strings");
    return result;
}
Values read_ini(const fs::path& path,bool catalogue=false){
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read config/catalogue file");
    Values values;std::string line,section;bool first=true;
    while(std::getline(input,line)){
        if(first&&line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);first=false;
        line=trim(std::move(line));
        if(line.empty()||line[0]=='#'||line[0]==';')continue;
        if(line.front()=='['&&line.back()==']'){section=line.substr(1,line.size()-2);continue;}
        const auto equals=line.find('=');if(equals==std::string::npos)continue;
        const auto key=trim(line.substr(0,equals));
        // Other GUI-only settings do not constrain a command-line process.
        const auto full_key=section+"/"+key;
        if(!catalogue && !full_key.starts_with("DocumentNames/") && full_key!="Application/Language" && full_key!="Paths/Templates" && full_key!="Paths/Localization" && full_key!="Paths/Materials" &&
            full_key!="Templates/Part" && full_key!="Templates/Assembly" && full_key!="Units/Length" &&
            full_key!="Units/Angle" && full_key!="Units/Mass" && full_key!="Units/Time" &&
            full_key!="Units/Temperature" && full_key!="Units/Stress" && full_key!="SheetMetal/CutTolerance")continue;
        if(catalogue&&section!="QtTranslations")continue;
        values[section+"/"+key]=catalogue?trim(line.substr(equals+1)):value_text(line.substr(equals+1));
    }
    if(input.bad())throw std::runtime_error("Failed while reading config/catalogue file");
    return values;
}
fs::path resolve(const fs::path& config,std::string text){
#ifdef _WIN32
    std::replace(text.begin(),text.end(),'\\','/');
#endif
    const auto path=fs::u8path(text);
    return fs::absolute(path.is_absolute()?path:config.parent_path()/path).lexically_normal();
}
}
std::string Settings::translate(const char* source,const char* context) const{
    // AssemblyWorkspaceWindow inherits QMainWindow::tr; the body factory
    // explicitly uses QObject::tr. Match those existing catalogue contexts.
    auto found=translations.find(std::string(context)+"|"+source);
    if(found==translations.end())found=translations.find(source);
    return found==translations.end()||found->second.empty()?std::string(source):found->second;
}
Settings load_settings(const fs::path& executable,const fs::path& working,const fs::path& explicit_config){
    const auto installed=distribution::locate_installation(executable);
    std::vector<fs::path> paths;
    if(installed && explicit_config.empty()) paths=distribution::config_layers(*installed);
    else {
        auto base=explicit_config;
        if(base.empty())for(const auto& candidate:{fs::current_path()/"config/config.ini",
            executable.parent_path()/"config/config.ini",executable.parent_path()/"../../config/config.ini"})
            if(fs::is_regular_file(candidate)){base=fs::absolute(candidate).lexically_normal();break;}
        if(!explicit_config.empty()&&!fs::is_regular_file(explicit_config))throw std::runtime_error("Config file does not exist");
        paths.push_back(base.empty()?fs::current_path()/"config/config.ini":base);
    }
    const auto local=working/"config.ini";
    if(fs::is_regular_file(local) && std::none_of(paths.begin(),paths.end(),[&](const auto& p){return fs::exists(p)&&fs::equivalent(p,local);}))paths.push_back(local);
    std::vector<std::pair<fs::path,Values>> layers;
    for(const auto& p:paths)layers.emplace_back(p,fs::is_regular_file(p)?read_ini(p):Values{});
    const auto supplied=[&](const char* key){
        for(auto it=layers.rbegin();it!=layers.rend();++it){
            const auto found=it->second.find(key);
            if(found!=it->second.end()&&!found->second.empty())return std::pair{it->first,found->second};
        }
        return std::pair{paths.front(),std::string{}};
    };
    const auto value=[&](const char* key,const char* fallback){const auto found=supplied(key);return found.second.empty()?std::string(fallback):found.second;};
    const auto configured_path=[&](const char* key,const char* fallback){const auto found=supplied(key);return resolve(found.first,found.second.empty()?fallback:found.second);};
    Settings result;
    const auto enabled=[&](const char* key){auto text=value(key,"false");std::ranges::transform(text,text.begin(),[](unsigned char c){return char(std::tolower(c));});return text=="true";};
    result.documents.normalize_document_name=DocumentNaming{enabled("DocumentNames/Uppercase"),enabled("DocumentNames/RemoveDiacritics"),enabled("DocumentNames/ReplaceSpaces")};
    result.stacked_tolerances=value("Dimensions/ToleranceLayout","inline")=="stacked";
    result.documents.templates={configured_path("Paths/Templates","templates"),
        fs::u8path(value("Templates/Part","START_PART.prtz")),fs::u8path(value("Templates/Assembly","START_ASSEMBLY.asmz")),"Těleso 1"};
    result.documents.templates.materials_directory=configured_path("Paths/Materials","materials");
    try {
        result.documents.templates.sheet_cut_tolerance=document::sheet_cut_tolerance({
            {"sheet_cut_tolerance",value("SheetMetal/CutTolerance","0.05")}});
    } catch(const std::invalid_argument& error) {
        result.documents.templates.sheet_cut_tolerance=.05;
        std::cerr<<"Warning: invalid SheetMetal/CutTolerance; using 0.05 mm. "<<error.what()<<'\n';
    }
    for(const auto& [key,fallback]:Values{{"Length","mm"},{"Angle","deg"},{"Mass","kg"},{"Time","s"},{"Temperature","C"},{"Stress","MPa"}})
        result.documents.units[key]=value(("Units/"+key).c_str(),fallback.c_str());
    const auto language=value("Application/Language","cs");
    const auto catalogue=configured_path("Paths/Localization","localization")/fs::u8path(language+".ini");
    if(fs::is_regular_file(catalogue))for(const auto& [key,text]:read_ini(catalogue,true))result.translations[key.substr(15)]=text;
    std::ifstream supplemental(catalogue.parent_path()/fs::u8path(language+".qt.json"));
    if(supplemental) {
        const auto messages=nlohmann::json::parse(supplemental);
        if(messages.is_object())for(const auto& [key,text]:messages.items())
            if(text.is_string())result.translations[key]=text.get<std::string>();
    }
    result.documents.templates.first_body_name=result.translate("Těleso 1","QObject");
    result.documents.templates.first_sheet_name=result.translate("List","QObject")+" 1";
    return result;
}
} // namespace zima::cli
