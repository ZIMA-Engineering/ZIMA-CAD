#include <zima/commands/dispatcher.hpp>
#include <iostream>
#include <stdexcept>
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
        std::cout<<"Command parsing, JSON validation, guards and error boundaries passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
