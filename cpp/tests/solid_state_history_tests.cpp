#include <zima/kernel/solid_state_history.hpp>
#include <iostream>
#include <limits>

using namespace zima::kernel;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejected(F&& action) {
    try{action();}catch(const std::invalid_argument&){return;}
    throw std::runtime_error("Invalid state history was accepted");
}
HistoryOperation source(std::string owner,std::string body) {
    HistoryOperation result;result.owner_id=std::move(owner);result.body.id=std::move(body);
    result.primitive=RevolutionRequest{};return result;
}
}
int main() {try {
    {
        HistoryOperation state;state.owner_id="restore";state.primitive=SolidStateRequest{true};
        auto child=source("child","one");
        const auto empty=history_fingerprint({state},1);
        state.solid_state_placements=std::make_shared<const std::vector<HistoryOperation>>(std::vector{child});
        const auto placed=history_fingerprint({state},1);
        require(empty!=placed,"Replay placement was omitted from calculation fingerprint");
        state.solid_state_placements=std::make_shared<const std::vector<HistoryOperation>>(std::vector{child});
        require(history_fingerprint({state},1)==placed,"Replay fingerprint depends on allocation identity");
        std::get<RevolutionRequest>(child.primitive).angle_degrees=30;
        state.solid_state_placements=std::make_shared<const std::vector<HistoryOperation>>(std::vector{child});
        require(history_fingerprint({state},1)!=placed,"Changed replay geometry reused a stale fingerprint");
        require(history_fingerprints({state}).back()==history_fingerprint({state},1),
            "Batch fingerprint lost transient replay geometry");
    }
    std::vector<HistoryOperation> history{source("a","one"),source("b","two"),
        source("fillet","one"),source("later","one")};
    FilletRequest fillet;fillet.radius_start=1.25;history[2].primitive=fillet;
    std::vector<SolidStateChange> changes{
        {2,"straight","one",false,true,false,.9,{}},
        {3,"longer","one",false,true,false,1.1,{}},
        {3,"restore","one",true,true,false,1,{}}};
    auto first=solid_states_before(history,changes,2);
    require(first.size()==2 && first.at("a").straight && first.at("a").coefficient==.9,
        "First state did not affect the preceding source");
    require(!first.at("b").straight && !first.contains("later"),"Body or history boundary leaked");
    auto longer=solid_states_before(history,std::span(changes).first(2),3);
    require(longer.at("a").source_index==0 && longer.at("a").coefficient==1.1,
        "Length coefficient accumulated or source was replaced");
    auto restored=solid_states_before(history,changes,4);
    require(!restored.at("a").straight && restored.at("a").coefficient==1 &&
        restored.at("a").state_owner=="restore" && !restored.at("later").straight,
        "Restore lost its boundary or retained the straight coefficient");
    require(std::get<FilletRequest>(history[2].primitive).radius_start==1.25 &&
        std::holds_alternative<RevolutionRequest>(history[0].primitive),
        "Timeline rewrote authored source or subsequent treatment");
    auto suppressed=changes;suppressed[0].suppressed=true;suppressed.resize(1);
    require(!solid_states_before(history,suppressed,3).at("a").straight,"Suppressed state was evaluated");
    auto selected=changes;selected.resize(1);selected[0].all=false;
    for(const auto& owners:std::vector<std::vector<std::string>>{{"b"},{"later"},{"fillet"},{"a","a"},{}}) {
        selected[0].owners=owners;
        rejected([&]{solid_states_before(history,selected,4);});
    }
    selected[0].owners={"a"};
    require(solid_states_before(history,selected,4).at("a").straight,"Explicit source selection failed");
    for(double value:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        selected[0].coefficient=value;rejected([&]{solid_states_before(history,selected,4);});
    }
    selected[0].coefficient=1;
    // All structural exclusions must also reject explicitly saved selections.
    for(int kind=0;kind<8;++kind) {
        auto excluded=history;
        auto& a=excluded[0];
        switch(kind) {
        case 0:a.suppressed=true;break;
        case 1:a.operation=BooleanOperation::Subtract;break;
        case 2:std::get<RevolutionRequest>(a.primitive).surface_result=true;break;
        case 3:a.sheet_operation=SheetOperation::Revolution;break;
        case 4:a.sheet_material=SheetMaterialDefinition{};break;
        case 5:a.feature_copy=BodyHistoryScope{};break;
        case 6:a.body.source_id="linked";break;
        case 7:{Sweep3DRequest sweep;sweep.make_solid=false;a.primitive=sweep;break;}
        }
        rejected([&]{solid_states_before(excluded,selected,4);});
    }
    auto duplicate=history;duplicate[1].owner_id="a";
    rejected([&]{solid_states_before(duplicate,changes,4);});
    auto invalid=changes;invalid[1].boundary=1;
    rejected([&]{solid_states_before(history,invalid,4);});
    invalid=changes;invalid[1].owner_id="a";
    rejected([&]{solid_states_before(history,invalid,4);});
    rejected([&]{solid_states_before(history,changes,5);});
    // A restore-all affects only sources currently straight, preserving another
    // source's authored state and the independent Body's state owner.
    changes.push_back({4,"other-body","two",false,true,false,.8,{}});
    changes.push_back({4,"new-source","one",false,false,false,.7,{"later"}});
    changes.push_back({4,"restore-new","one",true,true,false,1,{}});
    auto final=solid_states_before(history,changes,4);
    require(final.at("a").state_owner=="restore" && final.at("later").state_owner=="restore-new" &&
        final.at("b").coefficient==.8 && final.at("b").straight,"Restore-all crossed state or Body scope");
    require(solid_states_before(history,changes,2)==first,"Later states changed an earlier boundary");
    // Suppression recomputes from authored input, as required by Family states.
    changes.back().suppressed=true;
    require(solid_states_before(history,changes,4).at("later").coefficient==.7,
        "Suppression did not recover the preceding state");
    std::cout<<"Solid state timeline contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
