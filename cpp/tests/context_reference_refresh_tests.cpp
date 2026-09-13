#include "context_reference_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <iostream>
using namespace zima;namespace fs=std::filesystem;using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-8)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
void verify(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture fixture(kernel,dir);workspace::Workspace live;fixture.load(live);
    auto working_directory=dir;command_host::Host host(live,kernel,working_directory);run(host,"component.activate",{{"instance_path",fixture.target_path.encoded()}});
    const auto current=[&]{return workspace::document_sketch(live,fixture.target.document_id,fixture.sketch_id);};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=fixture.sketch_id;return run(host,name,std::move(args)).data;};
    // Initial cache coordinates were independently constructed analytically.
    // Normalize their floating-point representation through the displayed frame
    // once, then require repeated refresh to preserve the exact saved state.
    command("sketch.reference.refresh");
    const auto offset=command("sketch.offset.create",{{"source",fixture.curve_id},{"distance_mm",.1},{"flipped",true}}).at("geometry").get<std::string>();
    command("sketch.curve.retain",{{"geometry",fixture.curve_id},{"intervals",{{.1,.9}}}});
    const auto trims=current().curve_trims;const auto before=current().serialized();
    require(!command("sketch.reference.refresh").at("changed").get<bool>(),"Unchanged reference created a transaction");
    const auto top_revision=live.open_assembly(fixture.top.document_id)->session.revision(),top_generation=live.open_assembly(fixture.top.document_id)->session.data_generation();
    const auto top_cache=live.open_assembly(fixture.top.document_id)->session.document().components.front().calculated_source;
    bool calculation_failed=false;try{static_cast<void>(live.prepare_assembly_calculation(fixture.top.document_id));}catch(const std::exception&){calculation_failed=true;}
    require(calculation_failed,"The fixture would not detect implicit cut calculation");
    auto moved=fixture.calculated;auto& edge=moved.back().mesh.original_references.edges.back();
    for(auto& p:edge.points)p.x+=.01;for(auto& p:edge.exact_spline->poles)p.x+=.01;
    live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved);
    const auto refreshed=command("sketch.reference.refresh");require(refreshed.at("changed").get<bool>()&&!refreshed.at("body_calculated").get<bool>()&&refreshed.at("broken_references").empty(),"Current reference was not refreshed without body calculation");
    require(current().curve_trims==trims&&current().external_references.size()==4,"Refresh lost trims or reference kinds");
    for(unsigned i=0;i<=256;++i){const auto p=kernel::bspline_value(current().supporting_curve(fixture.curve_id),i/256.);near((p.x-8)*(p.x-8)+(p.y-17.01)*(p.y-17.01),1);}
    const auto expected=sketcher::offset_curve_geometry(current().supporting_curve(fixture.curve_id),-.1);
    for(unsigned i=0;i<=256;++i){const auto a=kernel::bspline_value(current().supporting_curve(offset),i/256.),b=kernel::bspline_value(expected,i/256.);near(a.x,b.x);near(a.y,b.y);}
    require(live.open_assembly(fixture.top.document_id)->session.revision()==top_revision&&live.open_assembly(fixture.top.document_id)->session.data_generation()==top_generation&&
        live.open_assembly(fixture.top.document_id)->session.document().components.front().calculated_source.shares_with(top_cache),"Reference refresh mutated its Assembly");
    run(host,"undo");require(current().serialized()==before,"Refresh Undo did not restore the exact Sketch");run(host,"redo");
    const auto retained=current().points;auto absent=moved;absent.back().mesh.original_references.edges.pop_back();live.open_part(fixture.source.document_id)->session.commit(fixture.source,std::move(absent));
    require(command("sketch.reference.refresh").at("broken_references").size()==1&&current().points==retained,"Missing edge destroyed its last valid projected curve");
    live.open_part(fixture.source.document_id)->session.commit(fixture.source,moved);require(command("sketch.reference.refresh").at("broken_references").empty(),"Restored original edge did not repair its reference");
    const auto stable=current().serialized();const auto revision=live.open_part(fixture.target.document_id)->session.revision();
    run(host,"component.activate",{{"instance_path",fixture.other_target_path.encoded()}});
    const auto invalid=host.execute({{"command","sketch.reference.refresh"},{"arguments",{{"sketch",fixture.sketch_id}}}});
    require(!invalid.ok&&invalid.code=="context_reference"&&current().serialized()==stable&&live.open_part(fixture.target.document_id)->session.revision()==revision,"Another occurrence edited the stored context");
    run(host,"component.activate",{{"instance_path",fixture.target_path.encoded()}});
    static_cast<void>(live.remove(fixture.source.document_id));fixture.source.save(dir/"context-source.prtz",moved);
    require(!command("sketch.reference.refresh").at("changed").get<bool>()&&!live.open_part(fixture.source.document_id),"Closed native refresh opened a source or changed an equal reference");
    require(fs::remove(dir/"context-source.prtz"),"Cannot remove the owned source fixture");
    require(command("sketch.reference.refresh").at("broken_references").size()==4&&current().points==retained,
        "Missing native file erased or rebound its references");
    fixture.source.save(dir/"context-source.prtz",moved);require(command("sketch.reference.refresh").at("broken_references").empty(),"Restored missing file did not repair its references");
    auto wrong=document::PartDocument::create_default();wrong.save(dir/"context-source.prtz",{});
    require(command("sketch.reference.refresh").at("broken_references").size()==4&&current().points==retained,"Replaced source was rebound or destroyed the last geometry");
    fixture.source.save(dir/"context-source.prtz",moved);require(command("sketch.reference.refresh").at("broken_references").empty(),"Restored native source did not repair all four references");
    run(host,"save");const auto reopened=document::PartDocument::load(dir/"context-target.prtz");require(reopened.sketches.front().serialized()==current().serialized(),"Native Part lost refreshed reference data");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-context-refresh-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Exact context refresh, trim and offset propagation, broken references, Undo/Redo and native persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
