#include <zima/document/feature_serialization.hpp>
#include <zima/document/part_document.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace zima::document;
namespace {
void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
template<class F> void rejected(F action) {bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"Invalid Feature Up To data was accepted");}
FeatureParameters fixture() {
    FeatureParameters p;
    for(std::size_t side=0;side<2;++side) {
        auto& end=p.sides[side];end.operation=FeatureSideOperation::Extrusion;end.extrusion_extent=EndCondition::UpTo;
        for(unsigned row=0;row<3;++row) {
            ExtrusionParameters::EndTarget target;target.reference={"source-"+std::to_string(row),"face","copy-"+std::to_string(side)};
            target.label="Authored target "+std::to_string(row);end.targets.push_back(target);
        }
        end.up_to.length_correction=side?-2.75:1.25;end.up_to.angle_correction_degrees=side?12.5:-7.5;
        end.up_to.primary_cap_source=end.targets.front().reference;
        for(std::size_t row=1;row<3;++row) {
            end.up_to.set_retained_side(end.targets[row-1].reference,end.targets[row].reference,true);
            end.up_to.set_retained_side(end.targets[row].reference,end.targets[row-1].reference,false);
        }
    }
    return p;
}
}
int main(){try {
    unsigned cases=0;const auto base=fixture();
    const auto neutral=serialize_feature_parameters(FeatureParameters{});
    for(const auto& side:neutral.at("sides"))check(!side.contains("up_to"),"Neutral Feature limit metadata is redundantly stored");++cases;
    for(std::size_t side=0;side<2;++side)for(bool angular:{false,true}) {
        FeatureParameters p;
        if(angular)p.sides[side].up_to.angle_correction_degrees=-0.;else p.sides[side].up_to.length_correction=-0.;
        const auto reopened=load_feature_parameters(nlohmann::json::parse(serialize_feature_parameters(p).dump()));
        check(std::signbit(angular?reopened.sides[side].up_to.angle_correction_degrees:reopened.sides[side].up_to.length_correction),
            "Otherwise neutral correction lost an authored negative zero");++cases;
    }
    for(bool reversed:{false,true})for(bool symmetric:{false,true})for(bool angular:{false,true}) {
        auto p=base;p.reversed_sides=reversed;p.symmetric=symmetric;
        for(auto& side:p.sides)if(angular){side.operation=FeatureSideOperation::Revolution;side.rotation_extent=FeatureRotationExtent::UpTo;}
        const auto stored=serialize_feature_parameters(p);const auto reopened=load_feature_parameters(nlohmann::json::parse(stored.dump()));
        check(p==reopened,"Feature corrections/retained sides did not round-trip");
        check(reopened.effective_side(1).up_to==reopened.sides[symmetric?0:1].up_to,"Symmetry lost its logical source side's limit settings");
        check(serialize_feature_parameters(reopened)==stored,"Reopening changed Feature limit data");++cases;
    }
    for(std::size_t side=0;side<2;++side)for(bool angular:{false,true}) {
        auto p=base;auto& up_to=p.sides[side].up_to;
        if(angular)up_to.angle_correction_degrees=-0.;else up_to.length_correction=-0.;
        const auto reopened=load_feature_parameters(nlohmann::json::parse(serialize_feature_parameters(p).dump()));
        check(std::signbit(angular?reopened.sides[side].up_to.angle_correction_degrees:reopened.sides[side].up_to.length_correction),
            "Feature correction signed zero was normalized during persistence");++cases;
    }
    {
        auto p=base;auto& side=p.sides[0];const auto choices=side.up_to.retained_patches;
        std::ranges::reverse(side.targets);prune_feature_up_to_patch_choices(side);
        check(side.up_to.retained_patches==choices,"Reversing row display order transferred or deleted an unchanged adjacency choice");
        std::swap(side.targets[0],side.targets[1]);prune_feature_up_to_patch_choices(side);
        check(side.up_to.retained_patches.size()==2,"Row reordering did not discard exactly the changed adjacency");
        for(const auto& choice:side.up_to.retained_patches)check(base.sides[0].up_to.retained_side(choice.source,choice.adjacent)==choice.positive,
            "Reordering transferred a retained side to another source pair");validate_feature_parameters(p);++cases;
        side.targets.erase(side.targets.begin()+1);prune_feature_up_to_patch_choices(side);
        check(side.up_to.retained_patches.empty(),"Removing a source left stale adjacency choices");validate_feature_parameters(p);++cases;
    }
    for(double bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto p=base;p.sides[0].up_to.length_correction=bad;rejected([&]{serialize_feature_parameters(p);});++cases;
        p=base;p.sides[1].up_to.angle_correction_degrees=bad;rejected([&]{serialize_feature_parameters(p);});++cases;
    }
    for(double bad:{-360.,360.,720.}) {auto p=base;p.sides[0].up_to.angle_correction_degrees=bad;rejected([&]{serialize_feature_parameters(p);});++cases;}
    {
        auto p=base;p.sides[0].up_to.retained_patches.push_back(p.sides[0].up_to.retained_patches.front());
        rejected([&]{serialize_feature_parameters(p);});++cases;
        p=base;p.sides[0].up_to.retained_patches.front().adjacent=p.sides[0].targets[2].reference;
        rejected([&]{serialize_feature_parameters(p);});++cases;
        p=base;p.sides[0].up_to.retained_patches.front().source.owner_id="missing";
        rejected([&]{serialize_feature_parameters(p);});++cases;
        auto stored=serialize_feature_parameters(base);stored["sides"][0]["up_to"].erase("angle_correction_degrees");
        rejected([&]{load_feature_parameters(stored);});++cases;
    }
    {
        auto part=PartDocument::create_default();auto sketch=zima::sketcher::Sketch::create_default();
        auto feature=PartDocument::create_feature_container(sketch.id);feature.feature=base;feature.feature.sketch_id=sketch.id;
        sketch.owner_container_id=feature.id;part.sketches={sketch};part.history={feature};
        BodyHistoryGraph graph;static_cast<void>(graph.create_body("Limits"));graph.insert({PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
        const auto stored=part.serialized();const auto reopened=PartDocument::from_serialized(stored);
        check(reopened.history.front().feature==feature.feature,"Native Part serialization lost Feature Up To intent");++cases;
    }
    std::cout<<"PASS "<<cases<<" Feature Up To definition checks: native round-trip, both corrections, reversal/symmetry, signed zero, semantic adjacency through reorder/removal and atomic invalid-data rejection.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
