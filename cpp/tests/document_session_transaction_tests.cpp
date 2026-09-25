#include "profile_solid_fixture.hpp"
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <cmath>
#include <type_traits>
#include <stdexcept>

namespace {
using namespace zima;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Snapshot {
    std::uint64_t revision,generation;
    bool dirty,undo,redo;
    std::string name,identifiers;
    std::map<std::string,std::string> parameters;
    const kernel::BodyResult* geometry;
    double volume;
    explicit Snapshot(const document::DocumentSession& session):revision(session.revision()),generation(session.data_generation()),
        dirty(session.is_dirty()),undo(session.can_undo()),redo(session.can_redo()),name(session.document().name),
        identifiers(session.document().dimension_identifiers.serialized()),parameters(session.document().user_parameters),
        geometry(session.calculated_boundaries().data()),volume(session.calculated_boundaries().back().volume){}
    void unchanged(const document::DocumentSession& session)const{
        require(session.revision()==revision && session.data_generation()==generation,"Rejected Part transaction changed revision/generation");
        require(session.is_dirty()==dirty && session.can_undo()==undo && session.can_redo()==redo,"Rejected Part transaction changed save/history state");
        require(session.document().name==name && session.document().dimension_identifiers.serialized()==identifiers &&
            session.document().user_parameters==parameters,"Rejected Part transaction changed document/identifiers/physical values");
        require(session.calculated_boundaries().data()==geometry && session.calculated_boundaries().back().volume==volume,
            "Rejected Part transaction replaced calculated geometry");
    }
};
template<class Action> void rejects(document::DocumentSession& session,Action action){
    const Snapshot before(session);bool failed=false;try{action();}catch(const std::exception&){failed=true;}
    require(failed,"Invalid Part transaction was accepted");before.unchanged(session);
}
void verify(){
    auto part=document::PartDocument::create_default();
    auto box=test::rectangular_feature(part,{10,10,10});
    part.history.push_back(box);part.relations.push_back({"capacity","1 / (2000 - round(model.volume))"});
    kernel::OcctKernel kernel;const auto original=kernel.evaluate_history(part.kernel_operations());
    require(std::abs(original.back().volume-1000)<1e-7,"Fixture must have independently known volume 1000 mm3");
    document::DocumentSession session(part,original);
    auto renamed=session.document();renamed.name="Edited Part";session.commit(renamed,original);
    require(session.undo() && session.can_redo(),"Fixture has no Redo state");session.mark_saved();
    auto enlarged=part;test::resize_rectangular_feature(enlarged,enlarged.history.front(),{20,10,10});
    const auto changed=kernel.evaluate_history(enlarged.kernel_operations());
    require(std::abs(changed.back().volume-2000)<1e-7,"Fixture must have independently known volume 2000 mm3");
    rejects(session,[&]{session.commit(enlarged,changed);});
    rejects(session,[&]{session.replace(enlarged,changed);});
    rejects(session,[&]{session.update_calculated_boundaries(changed);});
    auto invalid=session.document();invalid.document_units["Length"]="unknown";
    rejects(session,[&]{session.commit(invalid,original);});
    rejects(session,[&]{session.replace(invalid,original);});
    invalid=session.document();invalid.dimension_identifiers={};
    invalid.dimension_identifiers.synchronize({{"different owner","different dimension","conflict"}});
    rejects(session,[&]{session.commit(invalid,original);});
    invalid=session.document();invalid.history.front().id.clear();
    rejects(session,[&]{session.commit(invalid,original);});
    rejects(session,[&]{session.replace(invalid,original);});
    const auto generation=session.data_generation();
    require(session.redo() && session.document().name=="Edited Part" && session.data_generation()==generation+1,
        "Rejected operations lost Redo or advanced its generation incorrectly");
    require(session.undo() && session.document().name==part.name,"Rejected operations lost Undo");
    auto branch=session.document();branch.name="New branch";const auto revision=session.revision();
    session.commit(branch,original);
    require(session.revision()==revision+2 && !session.can_redo(),"Rejected operations consumed revisions or preserved obsolete Redo");
    require(session.undo() && session.redo() && session.document().name=="New branch","Branch history cannot be replayed");
    session.mark_saved();const auto before_refresh=session.revision();
    session.update_calculated_boundaries(original);
    require(session.is_dirty() && session.revision()==before_refresh,"Successful recalculation did not preserve edit history");
    // Growing history must retain the actual calculated allocations. This
    // catches accidental deep copies of every older body during vector growth.
    std::vector<const kernel::BodyResult*> allocations{session.calculated_boundaries().data()};
    for(int index=0;index<24;++index){
        auto next=session.document();next.name="History "+std::to_string(index);session.commit(std::move(next),original);
        allocations.push_back(session.calculated_boundaries().data());
    }
    document::DocumentSession copied(session);const auto copied_name=copied.document().name;
    require(copied.calculated_boundaries().data()!=session.calculated_boundaries().data(),"Explicit session copy aliases mutable calculated state");
    for(std::size_t index=allocations.size()-1;index>0;--index){
        require(session.undo() && session.calculated_boundaries().data()==allocations[index-1],"History growth copied a retained calculated body");
    }
    require(copied.document().name==copied_name && copied.can_undo(),"Undo changed an independent session copy");
    document::DocumentSession assigned(part,original);assigned=copied;
    require(copied.undo() && assigned.document().name==copied_name,"Copy assignment shares mutable history");
    assigned=assigned;require(assigned.document().name==copied_name,"Self-assignment lost history");
    static_assert(std::is_nothrow_move_constructible_v<document::DocumentSession> &&
        std::is_nothrow_move_assignable_v<document::DocumentSession>);
    const auto moved_geometry=assigned.calculated_boundaries().data();
    document::DocumentSession moved(std::move(assigned));
    require(moved.calculated_boundaries().data()==moved_geometry && moved.undo(),"Moving session copied geometry or lost history");
    require(session.redo() && session.calculated_boundaries().data()==allocations[1],"Redo copied retained geometry");
    const auto before_replace=session.data_generation();session.replace(part,original);
    require(!session.is_dirty() && !session.can_undo() && !session.can_redo() && session.revision()==0 &&
        session.data_generation()==before_replace+1,"Successful replace did not reset history atomically");
}
}
int main(){try{verify();std::cout<<"Part transaction rejection preserves geometry, physical values, save state and history\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
