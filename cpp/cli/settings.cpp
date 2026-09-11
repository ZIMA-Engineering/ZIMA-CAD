#include "settings.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

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
        if(!catalogue && full_key!="Application/Language" && full_key!="Paths/Templates" && full_key!="Paths/Localization" &&
            full_key!="Templates/Part" && full_key!="Templates/Assembly" && full_key!="Units/Length" &&
            full_key!="Units/Angle" && full_key!="Units/Mass" && full_key!="Units/Time" &&
            full_key!="Units/Temperature" && full_key!="Units/Stress")continue;
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
    auto base=explicit_config;
    if(base.empty())for(const auto& candidate:{fs::current_path()/"config/config.ini",
        executable.parent_path()/"config/config.ini",executable.parent_path()/"../../config/config.ini"})
        if(fs::is_regular_file(candidate)){base=fs::absolute(candidate).lexically_normal();break;}
    if(!explicit_config.empty()&&!fs::is_regular_file(explicit_config))throw std::runtime_error("Config file does not exist");
    Values primary=base.empty()?Values{}:read_ini(base);
    if(base.empty())base=fs::current_path()/"config/config.ini";
    const auto local=working/"config.ini";
    const bool use_local=fs::is_regular_file(local)&&(!fs::exists(base)||!fs::equivalent(base,local));
    const auto overrides=use_local?read_ini(local):Values{};
    const auto value=[&](const char* key,const char* fallback){
        if(const auto it=overrides.find(key);it!=overrides.end()&&!it->second.empty())return it->second;
        const auto it=primary.find(key);return it==primary.end()?std::string(fallback):it->second;
    };
    const auto configured_path=[&](const char* key,const char* fallback){
        const auto it=overrides.find(key);
        return resolve(it!=overrides.end()&&!it->second.empty()?local:base,value(key,fallback));
    };
    Settings result;
    result.documents.templates={configured_path("Paths/Templates","templates"),
        fs::u8path(value("Templates/Part","start_part.prtz")),fs::u8path(value("Templates/Assembly","start_assembly.asmz")),"Těleso 1"};
    for(const auto& [key,fallback]:Values{{"Length","mm"},{"Angle","deg"},{"Mass","kg"},{"Time","s"},{"Temperature","C"},{"Stress","MPa"}})
        result.documents.units[key]=value(("Units/"+key).c_str(),fallback.c_str());
    const auto language=value("Application/Language","cs");
    const auto catalogue=configured_path("Paths/Localization","localization")/fs::u8path(language+".ini");
    if(fs::is_regular_file(catalogue))for(const auto& [key,text]:read_ini(catalogue,true))result.translations[key.substr(15)]=text;
    result.documents.templates.first_body_name=result.translate("Těleso 1","QObject");
    return result;
}
} // namespace zima::cli
