#include <zima/commands/dispatcher.hpp>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace zima::commands {
Result Result::success(Json data) { return {true,"ok",{},std::move(data)}; }
Result Result::failure(std::string code,std::string message) {
    return {false,std::move(code),std::move(message),Json::object()};
}
Json Result::json() const {
    return {{"protocol","zima-cad.commands/1"},{"ok",ok},{"code",code},{"message",message},{"data",data}};
}
void Dispatcher::add(Command command, Handler handler) {
    if(command.name.empty() || !handler || entries_.contains(command.name))
        throw std::invalid_argument("Invalid or duplicate command");
    const auto name=command.name;
    entries_.emplace(name,Entry{std::move(command),std::move(handler)});
}
void Dispatcher::set_guard(Guard guard) { guard_=std::move(guard); }
Json Dispatcher::catalog() const {
    Json commands=Json::array();
    for(const auto& [name,entry]:entries_) {
        Json arguments=Json::array();
        for(const auto& argument:entry.command.arguments)
            arguments.push_back({{"name",argument.name},{"type","string"},{"required",argument.required}});
        commands.push_back({{"name",name},{"description",entry.command.description},
            {"arguments",arguments},{"changes_state",entry.command.changes_state}});
    }
    return commands;
}
Result Dispatcher::execute(const Json& request) const {
    try {
        if(!request.is_object() || !request.contains("command") || !request["command"].is_string())
            return Result::failure("invalid_request","Expected an object with a string command.");
        for(const auto& [key,value]:request.items())
            if(key!="command" && key!="arguments")
                return Result::failure("invalid_request","Unknown request field: "+key);
        const auto name=request["command"].get<std::string>();
        const auto found=entries_.find(name);
        if(found==entries_.end())return Result::failure("unknown_command","Unknown command: "+name);
        const auto arguments=request.value("arguments",Json::object());
        if(!arguments.is_object())return Result::failure("invalid_arguments","Arguments must be an object.");
        const auto& entry=found->second;
        for(const auto& [key,value]:arguments.items()) {
            bool known=false;
            for(const auto& argument:entry.command.arguments)if(argument.name==key)known=true;
            if(!known || !value.is_string())return Result::failure("invalid_arguments","Unknown or non-string argument: "+key);
        }
        for(const auto& argument:entry.command.arguments)
            if(argument.required && (!arguments.contains(argument.name) || arguments[argument.name].get_ref<const std::string&>().empty()))
                return Result::failure("missing_argument","Missing argument: "+argument.name);
        if(guard_) { auto result=guard_(entry.command);if(!result.ok)return result; }
        return entry.handler(arguments);
    } catch(const std::exception& error) {
        return Result::failure("operation_failed",error.what());
    }
}
Result Dispatcher::execute_text(std::string_view text) const {
    if(text.size()>65536)return Result::failure("request_too_large","Command exceeds 64 KiB.");
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return Result::failure("empty_command","Enter a command.");
    if(text[first]=='{') {
        try { return execute(Json::parse(text)); }
        catch(const Json::exception& error) { return Result::failure("invalid_json",error.what()); }
    }
    std::vector<std::string> words;
    std::string word;bool quoted=false,started=false;
    for(std::size_t i=0;i<text.size();++i) {
        const char c=text[i];
        if(c=='"') {quoted=!quoted;started=true;}
        else if(c=='\\' && quoted && i+1<text.size() && text[i+1]=='"') {word+='"';++i;started=true;}
        else if(!quoted && std::isspace(static_cast<unsigned char>(c))) {
            if(started){words.push_back(std::move(word));word.clear();started=false;}
        } else {word+=c;started=true;}
    }
    if(quoted)return Result::failure("invalid_text","Unclosed quote.");
    if(started)words.push_back(std::move(word));
    const auto found=entries_.find(words.front());
    if(found==entries_.end())return Result::failure("unknown_command","Unknown command: "+words.front());
    if(words.size()-1>found->second.command.arguments.size())
        return Result::failure("invalid_arguments","Too many arguments.");
    Json arguments=Json::object();
    for(std::size_t i=1;i<words.size();++i)arguments[found->second.command.arguments[i-1].name]=words[i];
    return execute({{"command",words.front()},{"arguments",arguments}});
}
} // namespace zima::commands
