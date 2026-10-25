#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/document/feature_direction.hpp>
#include <zima/document/feature_rotation_span.hpp>
#include <iostream>
#include <cmath>
#include <limits>

using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* command,Json args=Json::object()) {
        auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);return result.data;
    };
    run("new",{{"type","part"},{"name","direction"}});
    const auto sketch=run("sketch.create",{{"name","Profile"},{"plane","XY"}}).at("sketch");
    run("sketch.rectangle.create",{{"sketch",sketch},{"first",{-3,-2}},{"second",{3,2}}});
    document::FeatureParameters parameters;parameters.sides[0].length=12;
    parameters.sides[1].operation=document::FeatureSideOperation::Extrusion;parameters.sides[1].length=5;
    auto definition=document::serialize_feature_parameters(parameters);definition.erase("sketch_id");
    for(auto& side:definition.at("sides"))if(side.at("operation")=="extrusion")side["operation"]="extrude";
    const auto id=run("feature.create",{{"sketch",sketch},{"parameters",definition}}).at("container").get<std::string>();
    auto* state=live.open_part(live.active_document_id());
    const auto value=[&]()->const document::HistoryContainer&{return *state->session.document().find_container(id);};
    const auto original=value();const auto& doc=state->session.document();
    const auto profile=[&]()->const sketcher::Sketch& {
        const auto found=std::ranges::find(state->session.document().sketches,value().feature.sketch_id,&sketcher::Sketch::id);
        if(found==state->session.document().sketches.end())throw std::runtime_error("Feature profile Sketch is missing");
        return *found;
    };
    const auto original_profile=profile().serialized();
    const auto direction=doc.dimension_identifiers.identifier(id,"parameter:direction");check(!direction.empty(),"Direction has no stable dimension identifier");
    const auto dimensions=workspace::relation_dimensions(doc);const auto& input=dimensions.at(direction);
    check(input.native_scale==1&&input.input.value.units==std::array<int,3>{}&&input.input.writable,"Direction is not a writable scalar");
    for(const auto& unit:{"in","mm"}) {
        auto converted=doc;auto units=converted.document_units;units["Length"]=unit;units["Angle"]="rad";
        workspace::convert_relation_units(converted,units);
        const auto field=workspace::relation_dimensions(converted).at(direction);
        check(field.native_scale==1&&std::get<double>(field.input.value.data)==1,"Unit conversion scaled direction");
    }
    const auto equations=[&](bool reverse) {
        const auto& result=state->session.calculated_boundaries().back();check(result.calculation_errors.empty(),"Direction calculation failed");
        check(std::abs(result.volume-24*17)<1e-6,"Direction changed the profile volume");
        double low=1e100,high=-1e100;for(auto p:result.mesh.vertices){low=std::min(low,p.z);high=std::max(high,p.z);}
        check(std::abs(low-(reverse?-12:-5))<1e-6&&std::abs(high-(reverse?5:12))<1e-6,"Direction did not reverse physical sides");
        check(value().feature.sides==original.feature.sides&&value().feature.sketch_id==original.feature.sketch_id,"Direction exchanged panels or source identity");
        check(profile().serialized()==original_profile,"Direction mirrored or otherwise changed the profile Sketch");
    };
    for(double sign:{-1.,1.,-1.}) {
        const auto previous=value().feature;
        run("document.relations.set",{{"relations",direction+" = "+(sign<0?"-1":"1")+"\n"}});
        check(value().feature==previous,"Storing Relations calculated or changed the Feature");
        for(int repeat=0;repeat<3;++repeat){run("regenerate");equations(sign<0);}
    }
    for(const auto* expression:{"0","2","0.5"}) {
        run("document.relations.set",{{"relations",direction+" = "+expression+"\n"}});
        const auto before=state->session.document();const auto revision=state->session.revision();
        const auto rejected=host.execute({{"command","regenerate"},{"arguments",Json::object()}});
        check(!rejected.ok&&state->session.revision()==revision&&state->session.document().serialized()==before.serialized(),"Invalid scalar direction changed the document");
        run("document.relations.set",{{"relations",direction+" = -1\n"}});
    }
    run("document.relations.set",{{"relations",""}});
    run("value_lock.set",{{"object",id},{"key","direction"},{"locked",true}});
    check(!workspace::relation_dimensions(state->session.document()).at(direction).input.writable,"Locked direction remained writable");
    const auto revision=state->session.revision();
    check(!host.execute({{"command","document.relations.set"},{"arguments",{{"relations",direction+" = 1\n"}}}}).ok&&state->session.revision()==revision,"Relation changed a locked direction");
    run("value_lock.set",{{"object",id},{"key","direction"},{"locked",false}});
    run("document.relations.set",{{"relations",direction+" = 1\n"}});run("regenerate");equations(false);
    run("undo");equations(true);run("redo");equations(false);run("save");
    std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(directory/"direction.prtz",&cache);
    check(reopened.dimension_identifiers.identifier(id,"parameter:direction")==direction&&reopened.find_container(id)->feature==value().feature,"Native reopen changed direction identity or panels");
    const auto cold=kernel.evaluate_history(reopened.kernel_operations());check(std::abs(cold.back().volume-cache.back().volume)<1e-6,"Cold direction regeneration differs");
    for(std::size_t side:{0u,1u})for(bool reversed:{false,true}) {
        auto p=original.feature;p.reversed_sides=reversed;const auto untouched=p.sides[1-side];
        check(document::set_feature_signed_length(p,side,-7)&&p.reversed_sides!=reversed&&p.sides[side].length==7&&p.sides[1-side]==untouched,"Negative extent lost side ownership");
        const auto valid=p;for(double invalid:{0.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(!document::set_feature_signed_length(p,side,invalid)&&p==valid,"Rejected signed extent changed parameters");
        p=original.feature;p.reversed_sides=reversed;
        p.sides[side].operation=p.sides[1-side].operation=document::FeatureSideOperation::Revolution;
        p.sides[1-side].angle_degrees=125;
        const auto other=p.sides[1-side];
        check(document::set_feature_signed_angle(p,side,-65)&&p.reversed_sides!=reversed&&p.sides[side].angle_degrees==65&&p.sides[1-side]==other,"Negative angle lost side ownership");
        check(document::feature_rotation_drag_limit(p,side)==235,"Double Revolution drag can pass the opposite end");
        const auto angle_valid=p;for(double invalid:{0.,361.,-361.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
            check(!document::set_feature_signed_angle(p,side,invalid)&&p==angle_valid,"Rejected signed angle changed parameters");
        p.symmetric=true;check(document::feature_rotation_drag_limit(p,side)==180,"Symmetric Revolution drag can overlap itself");
        p.symmetric=false;p.sides[1-side].operation=document::FeatureSideOperation::Extrusion;
        check(document::feature_rotation_drag_limit(p,side)==360,"Mixed Revolution/Extrusion was restricted by an inactive angle");
        p.sides[1-side].operation=document::FeatureSideOperation::Revolution;p.sides[1-side].rotation_extent=document::FeatureRotationExtent::Full;
        check(document::feature_rotation_drag_limit(p,side)==0,"Angle drag can replace an opposite Full Revolution");
    }
}
}
int main(){try {const auto root=fs::canonical(fs::temp_directory_path()),path=root/("zima-feature-direction-"+document::PartDocument::create_default().document_id);
    check(fs::create_directory(path),"Cannot create direction test directory");verify(kernel::OcctKernel{},path);check(path.parent_path()==root,"Unsafe cleanup");fs::remove_all(path);
    std::cout<<"Feature direction: scalar units, repeated Relations, panels, locks, rejection, Undo/Redo, signed extents and native regeneration passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
