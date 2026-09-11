#include <zima/commands/dispatcher.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace zima::commands;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        Dispatcher dispatcher;int writes=0;bool locked=false;
        dispatcher.add({"open","Open",{{"path",true}},true},[&](const Json& args){++writes;return Result::success(args);});
        dispatcher.add({"read","Read",{},false},[](const Json&){return Result::success({{"value",42}});});
        dispatcher.set_guard([&](const Command& command){return locked&&command.changes_state?Result::failure("busy","Busy"):Result::success();});
        const auto text=dispatcher.execute_text(R"(open "C:\My parts\žluťoučký díl.prtz")");
        check(text.ok && text.data["path"]==R"(C:\My parts\žluťoučký díl.prtz)","Windows path/UTF-8 changed");
        const auto json=dispatcher.execute_text(R"({"command":"open","arguments":{"path":"C:/My parts/a.prtz"}})");
        check(json.ok && json.data["path"]=="C:/My parts/a.prtz","JSON dispatch failed");
        const auto before=writes;
        for(const auto& bad:{"open","open a b","open \"unfinished","{broken","cmd /c erase x",""})
            check(!dispatcher.execute_text(bad).ok,"Invalid command was accepted");
        check(!dispatcher.execute({{"command","open"},{"arguments",{{"path",3}}}}).ok,"Wrong type accepted");
        check(!dispatcher.execute({{"command","open"},{"arguments",{{"path","x"},{"extra","y"}}}}).ok,"Unknown argument accepted");
        check(!dispatcher.execute({{"command","read"},{"shell","x"}}).ok,"Unknown request field accepted");
        check(!dispatcher.execute_text(std::string(65537,'x')).ok,"Oversized request accepted");
        check(writes==before,"Validation executed a mutation");
        locked=true;
        check(dispatcher.execute_text("open x").code=="busy" && writes==before,"Guard did not prevent mutation");
        check(dispatcher.execute_text("read").ok,"Guard blocked a read");
        dispatcher.add({"failure","Failure",{},false},[](const Json&)->Result{throw std::runtime_error("fixture error");});
        check(dispatcher.execute_text("failure").code=="operation_failed","Exception escaped result boundary");
        check(dispatcher.catalog().size()==3,"Catalog mismatch");
        check(dispatcher.execute_text("read").json().at("ok")==true,"Response envelope missing");
        using Type=ArgumentType;
        dispatcher.add({"structured","Typed",{{"length",true,Type::Number},{"count",true,Type::Integer},
            {"enabled",true,Type::Boolean},{"placement",true,Type::Object},{"points",true,Type::Array}},true},
            [&](const Json& args){++writes;return Result::success(args);});
        locked=false;
        const Json valid={{"length",1.25},{"count",3},{"enabled",true},
            {"placement",{{"owner","persisted-owner"},{"key","origin:plane:xy"}}},{"points",Json::array({1,2,3})}};
        const auto typed=dispatcher.execute({{"command","structured"},{"arguments",valid}});
        check(typed.ok && typed.data==valid,"Native typed arguments lost values or reference identities");
        const auto typed_text=dispatcher.execute_text(R"(structured 1.25 3 true "{\"owner\":\"persisted-owner\",\"key\":\"origin:plane:xy\"}" "[1,2,3]")");
        check(typed_text.ok && typed_text.data==valid,"Text typed arguments differ from JSON");
        const auto written=writes;
        for(const auto& [key,bad]:std::vector<std::pair<std::string,Json>>{
            {"length","1.25"},{"length",std::numeric_limits<double>::infinity()},{"length",nullptr},
            {"count",1.5},{"count",true},{"enabled",1},{"enabled","true"},
            {"placement",Json::array()},{"placement","{}"},{"points",Json::object()}}) {
            auto invalid=valid;invalid[key]=bad;
            check(dispatcher.execute({{"command","structured"},{"arguments",invalid}}).code=="invalid_arguments","Wrong typed argument accepted");
        }
        auto missing=valid;missing.erase("placement");
        check(dispatcher.execute({{"command","structured"},{"arguments",missing}}).code=="missing_argument","Missing required object accepted");
        check(!dispatcher.execute_text("structured 1.25 three true {} []").ok,"Malformed typed text accepted");
        check(writes==written,"Typed validation invoked a mutation");
        locked=true;check(dispatcher.execute({{"command","structured"},{"arguments",valid}}).code=="busy" && writes==written,"Typed request bypassed guard");
        auto catalog=dispatcher.catalog();bool found_typed=false;
        for(const auto& item:catalog)if(item.at("name")=="structured") {
            found_typed=true;check(item.at("arguments")[0].at("type")=="number" && item.at("arguments")[3].at("type")=="object","Catalog lost argument types");
        }
        check(found_typed,"Typed command missing from catalog");
        bool duplicate_rejected=false;
        try{dispatcher.add({"duplicate","",{{"a",true},{"a",false}},false},[](const Json&){return Result::success();});}
        catch(const std::invalid_argument&){duplicate_rejected=true;}
        check(duplicate_rejected,"Ambiguous duplicate argument declaration accepted");
        std::cout<<"Command parsing, JSON validation, guards and error boundaries passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
