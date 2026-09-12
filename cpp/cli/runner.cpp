#include "runner.hpp"
#include "settings.hpp"
#include <QGuiApplication>
#include <memory>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace zima::cli {
namespace {
using commands::Result;
namespace fs=std::filesystem;
constexpr std::size_t max_line=65536;
constexpr const char* usage=R"(ZIMA-CAD command line
Usage:
  zima-cad-cli [options] --command "COMMAND" [--command "COMMAND" ...]
  zima-cad-cli [options] --script commands.txt
  zima-cad-cli [options] --stdin

Options:
  --working-directory DIR   Existing directory for relative document paths.
  --config FILE             Base config.ini (project config.ini overrides it).
  --keep-going              Continue after failed commands; exit still reports failure.
  --help                    Show this help without loading documents or config.

Commands are the same text/JSON commands as the CAD console. One UTF-8 command
per line in scripts/stdin; blank lines and whole-line # comments are ignored.
Results: one JSON object per command on stdout, flushed immediately.
Diagnostics go to stderr. No implicit save at exit; use the save command.
Exit codes: 0 success, 1 command failure, 2 startup/input/output failure.
No input mode opens a GUI. --stdin ends at EOF; it prints no interactive prompt.
)";
struct Options {
    fs::path working=fs::current_path(),config,script;
    std::vector<std::string> commands;
    bool stdin_mode{},keep_going{},help{};
};
Options parse(const std::vector<std::string>& arguments){
    Options result;bool working_seen=false,config_seen=false,script_seen=false;
    for(std::size_t i=0;i<arguments.size();++i){
        const auto& option=arguments[i];
        const auto next=[&]() -> const std::string& {
            if(++i==arguments.size()||arguments[i].empty())throw std::invalid_argument("Missing value after "+option);
            return arguments[i];
        };
        if(option=="--help")result.help=true;
        else if(option=="--command")result.commands.push_back(next());
        else if(option=="--working-directory"){
            if(working_seen)throw std::invalid_argument("Repeated --working-directory");working_seen=true;
            result.working=fs::absolute(fs::u8path(next())).lexically_normal();
        }else if(option=="--config"){
            if(config_seen)throw std::invalid_argument("Repeated --config");config_seen=true;
            result.config=fs::absolute(fs::u8path(next())).lexically_normal();
        }else if(option=="--script"){
            if(script_seen)throw std::invalid_argument("Repeated --script");script_seen=true;
            result.script=fs::absolute(fs::u8path(next())).lexically_normal();
        }else if(option=="--stdin")result.stdin_mode=true;
        else if(option=="--keep-going")result.keep_going=true;
        else throw std::invalid_argument("Unknown option: "+option);
    }
    if(result.help){if(arguments.size()!=1)throw std::invalid_argument("Use --help on its own");return result;}
    const int modes=static_cast<int>(!result.commands.empty())+static_cast<int>(!result.script.empty())+static_cast<int>(result.stdin_mode);
    if(modes!=1)throw std::invalid_argument("Select exactly one input mode: --command, --script or --stdin (see --help)");
    if(!fs::is_directory(result.working))throw std::invalid_argument("Working directory does not exist");
    return result;
}
// Consume overlong lines without allocating unbounded memory, then continue at
// the next real line only when the caller explicitly selected --keep-going.
bool read_line(std::istream& input,std::string& line,bool& too_large){
    line.clear();too_large=false;bool any=false;char c;
    while(input.get(c)){
        any=true;if(c=='\n')break;
        if(line.size()<max_line+4)line+=c;else too_large=true;
    }
    if(input.bad()||(!input.eof()&&input.fail()))throw std::runtime_error("Command input read failed");
    return any;
}
}
int run(const std::vector<std::string>& arguments,const fs::path& executable,
    std::istream& input,const std::function<void(const std::string&)>& write){
    try{
        auto options=parse(arguments);
        if(options.help){write(usage);return 0;}
        std::ifstream script;
        if(!options.script.empty()){
            script.open(options.script,std::ios::binary);
            if(!script)throw std::runtime_error("Cannot open command script");
        }
        const auto settings=load_settings(executable,options.working,options.config);
        // Offscreen Qt supplies font metrics and PDF painting without any window.
        int graphics_argc=3;char app_name[]="zima-cad-cli",platform_option[]="-platform",platform_name[]="offscreen";
        char* graphics_argv[]={app_name,platform_option,platform_name,nullptr};
        std::unique_ptr<QGuiApplication> graphics;
        if(!QCoreApplication::instance())graphics=std::make_unique<QGuiApplication>(graphics_argc,graphics_argv);
        workspace::Workspace workspace;kernel::OcctKernel kernel;
        command_host::Options adapters;
        adapters.settings=[&]{return settings.documents;};
        adapters.translate=[&](const char* source){return settings.translate(source);};
        command_host::Host host(workspace,kernel,options.working,std::move(adapters));
        bool failed=false;
        const auto execute=[&](const std::string& text,bool too_large=false){
            Result result;
            if(too_large||text.size()>max_line)result=Result::failure("request_too_large","Command exceeds 64 KiB.");
            else {
                // Validate UTF-8 before any mutation; a malformed document name
                // must not first change the model and only then break JSON output.
                try{static_cast<void>(commands::Json(text).dump());}
                catch(const commands::Json::exception&){result=Result::failure("invalid_utf8","Command must be UTF-8.");}
                if(result.code.empty())result=host.execute_text(text);
            }
            std::string record;
            try{record=result.json().dump();}
            catch(const commands::Json::exception&){
                // Windows/third-party failure diagnostics may use a local code
                // page. Preserve the failure code and emit valid JSON. Never
                // silently replace IDs or names in a successful model response.
                if(result.ok)result=Result::failure("invalid_result","Command returned a non-UTF-8 result.");
                record=result.json().dump(-1,' ',false,commands::Json::error_handler_t::replace);
            }
            failed|=!result.ok;write(record+"\n");
            return result.ok||options.keep_going;
        };
        if(!options.commands.empty()){
            for(const auto& command:options.commands)if(!execute(command))break;
        }else{
            auto& stream=script.is_open()?static_cast<std::istream&>(script):input;
            std::string line;bool too_large=false,first=true;
            while(read_line(stream,line,too_large)){
                if(first&&line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);first=false;
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                const auto start=line.find_first_not_of(" \t\r");
                if(!too_large&&(start==std::string::npos||line[start]=='#'))continue;
                if(!execute(line,too_large))break;
            }
        }
        return failed?1:0;
    }catch(const std::exception& error){
        std::cerr<<Result::failure("cli_error",error.what()).json().dump(-1,' ',false,commands::Json::error_handler_t::replace)<<'\n';
        return 2;
    }
}
} // namespace zima::cli
