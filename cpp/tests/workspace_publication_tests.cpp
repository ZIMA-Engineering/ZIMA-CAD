#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <cstdint>
#include <type_traits>
#include <stdexcept>
using namespace zima;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void verify_assembly(const kernel::BodyResult& original,const kernel::BodyResult& changed,const std::string& source) {
    auto doc=assembly::AssemblyDocument::create_default();
    doc.components.push_back(assembly::AssemblyDocument::create_part_occurrence("Source",source,{},original));
    doc.relations.push_back({"capacity","1 / (2000 - round(model.volume))"});
    assembly::AssemblySession session(doc);auto renamed=session.document();renamed.name="Edited Assembly";session.commit(renamed);
    require(session.undo()&&session.can_redo(),"Assembly fixture has no Redo");session.mark_saved();
    const auto before=session.document();const auto* address=&session.document();
    const auto generation=session.data_generation(),revision=session.revision();
    const auto reject=[&](auto action){
        bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
        require(rejected,"Invalid Assembly update was accepted");
        require(session.data_generation()==generation&&session.revision()==revision&&!session.is_dirty()&&!session.can_undo()&&session.can_redo(),
            "Rejected Assembly update changed history/save/generation");
        require(&session.document()==address&&session.document().name==before.name&&session.document().user_parameters==before.user_parameters&&
            session.document().dimension_identifiers==before.dimension_identifiers&&
            session.document().components.front().calculated_source.shares_with(before.components.front().calculated_source),
            "Rejected Assembly update changed document or source geometry");
    };
    auto invalid=before;invalid.components.front().calculated_source=changed;
    reject([&]{session.commit(invalid);});reject([&]{session.replace(invalid);});reject([&]{session.update_dependency_snapshots(invalid);});
    auto units=before;units.document_units["Length"]="invalid";reject([&]{session.replace(units);});
    // The display-only source update intentionally does not evaluate physical
    // relations. The new volume would make the relation above divide by zero.
    session.update_source_geometry(invalid);
    require(session.document().components.front().calculated_source->volume==changed.volume && session.document().user_parameters==before.user_parameters &&
        session.revision()==revision&&!session.is_dirty()&&session.can_redo()&&session.data_generation()==generation+1&& &session.document()==address,
        "Source display refresh changed physical/history/save policy");
    session.update_source_geometry(before);require(session.redo()&&session.document().name=="Edited Assembly","Rejected update lost Assembly Redo");
    session.mark_saved();const auto current_revision=session.revision();session.update_dependency_snapshots(session.document());
    require(session.is_dirty()&&session.revision()==current_revision,"Explicit dependency refresh lost its dirty state or added an edit");
    std::vector<const assembly::AssemblyDocument*> states{&session.document()};
    for(unsigned i=0;i<24;++i){auto next=session.document();next.name=std::to_string(i);session.commit(next);states.push_back(&session.document());}
    assembly::AssemblySession copy(session);const auto copied_name=copy.document().name;
    for(std::size_t i=states.size()-1;i>0;--i)require(session.undo()&& &session.document()==states[i-1],"Growing Assembly history copied existing states");
    require(copy.document().name==copied_name&&copy.undo(),"Assembly copy shares mutable history");
    assembly::AssemblySession assigned(doc);assigned=copy;const auto assigned_name=assigned.document().name;
    require(copy.undo()&&assigned.document().name==assigned_name,"Assembly copy assignment shares history");
    session.replace(doc);require(!session.is_dirty()&&!session.can_undo()&&!session.can_redo()&&session.revision()==0,"Valid Assembly replacement did not reset state");
}
void verify_drawing(){
    auto doc=drawing::DrawingDocument::create_default();
    doc.dimension_identifiers.synchronize({{"retired owner","parameter:retired","Retired dimension"}});
    workspace::DrawingState state(doc,"drawing.drwz");auto renamed=state.document();renamed.name="Edited Drawing";state.commit(renamed);
    require(state.undo()&&state.can_redo(),"Drawing fixture has no Redo");state.mark_saved();
    const auto* address=&state.document();const auto generation=state.data_generation(),revision=state.revision();
    const auto identifiers=state.document().dimension_identifiers;
    const auto reject=[&](auto document){
        bool rejected=false;try{state.commit(std::move(document));}catch(const std::exception&){rejected=true;}
        require(rejected&& &state.document()==address&&state.data_generation()==generation&&state.revision()==revision&&
            !state.is_dirty()&&!state.can_undo()&&state.can_redo()&&state.document().dimension_identifiers==identifiers,
            "Rejected Drawing edit changed live state");
    };
    auto invalid=state.document();invalid.document_id="different drawing";reject(invalid);
    invalid=state.document();invalid.dimension_identifiers={};invalid.dimension_identifiers.synchronize({{"different owner","parameter:retired","Other"}});reject(invalid);
    require(state.redo()&&state.document().name=="Edited Drawing","Rejected Drawing edit lost Redo");
    std::vector<const drawing::DrawingDocument*> states{&state.document()};
    for(unsigned i=0;i<24;++i){auto next=state.document();next.name=std::to_string(i);state.commit(next);states.push_back(&state.document());}
    workspace::DrawingState copy(state);const auto copied_name=copy.document().name;
    require(copy.path==state.path&&copy.runtime_identity==state.runtime_identity,"Explicit Drawing copy lost save receipt identity/path");
    for(std::size_t i=states.size()-1;i>0;--i)require(state.undo()&& &state.document()==states[i-1],"Growing Drawing history copied existing states");
    require(copy.document().name==copied_name&&copy.undo(),"Drawing copy shares mutable history");
    workspace::DrawingState assigned(doc);assigned=copy;const auto assigned_name=assigned.document().name;
    require(copy.undo()&&assigned.document().name==assigned_name,"Drawing copy assignment shares history");
}
void verify(){
    static_assert(std::is_nothrow_move_constructible_v<workspace::DocumentState> &&
        std::is_nothrow_move_assignable_v<workspace::DocumentState>);
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    workspace::Workspace live;live.add_part(part,calculated);const auto id=part.document_id;
    const auto original=reinterpret_cast<std::uintptr_t>(live.open_part(id)->session.calculated_boundaries().data());
    auto next=part;next.name="Edited";live.open_part(id)->session.commit(next,calculated);
    const auto edited=reinterpret_cast<std::uintptr_t>(live.open_part(id)->session.calculated_boundaries().data());
    const auto generation=live.open_part(id)->session.data_generation(),revision=live.open_part(id)->session.revision();
    for(unsigned index=0;index<48;++index){
        if(index%3==0)live.add_assembly(assembly::AssemblyDocument::create_default());
        else if(index%3==1)live.add_drawing(drawing::DrawingDocument::create_default());
        else live.add_part(document::PartDocument::create_default());
        require(reinterpret_cast<std::uintptr_t>(live.open_part(id)->session.calculated_boundaries().data())==edited,
            "Growing the open-document list copied existing calculated Part geometry");
        require(live.open_part(id)->session.data_generation()==generation && live.open_part(id)->session.revision()==revision,
            "Opening another document modified the existing Part");
    }
    require(live.open_part(id)->session.undo(),"Opening another document lost Undo");
    require(reinterpret_cast<std::uintptr_t>(live.open_part(id)->session.calculated_boundaries().data())==original,
        "Growing the open-document list copied calculated Part history");
    auto copy=live;require(copy.open_part(id)->session.calculated_boundaries().data()!=live.open_part(id)->session.calculated_boundaries().data(),
        "Explicit Workspace copy aliases mutable Part geometry");
    require(copy.open_part(id)->session.redo()&&live.open_part(id)->session.document().name==part.name,"Workspace copy shares Part history");
    auto doubled=part;doubled.history.front().box.length=20;const auto changed=kernel.evaluate_history(doubled.kernel_operations());
    verify_assembly(calculated.back(),changed.back(),part.document_id);verify_drawing();
}
}
int main(){try{verify();std::cout<<"Opening mixed documents preserves Part geometry and history allocations\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
