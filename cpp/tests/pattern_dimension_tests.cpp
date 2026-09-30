#include <zima/document/pattern_dimensions.hpp>
#include <zima/document/part_document.hpp>
#include <iostream>
#include <stdexcept>

using namespace zima;
void check(bool value){if(!value)throw std::runtime_error("Pattern dimension assertion failed");}
template<class F> void rejects(F f){try{f();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Invalid pattern dimension accepted");}
int main(){try {
    document::DerivedCopyParameters copy;copy.source_id="source";copy.pattern=kernel::PatternRequest{};
    copy.pattern->origin={30,40,50};copy.pattern->linear[0].spacing=25;
    auto dimensions=document::pattern_dimensions("pattern",copy);
    check(dimensions.size()==2&&dimensions[0].value==25&&dimensions[1].value==4);
    check(dimensions[0].reference.owner_id=="pattern"&&dimensions[1].reference.semantic_key=="parameter:pattern:count:0");
    check(dimensions[1].label_only&&dimensions[1].unit_suffix.empty());
    check(dimensions[0].witness_first==copy.pattern->origin&&dimensions[0].witness_second.x==55);
    check(document::assign_pattern_dimension(copy,"pattern:count:0",7));
    check(copy.pattern->linear[0].count==7&&copy.source_id=="source");
    rejects([&]{document::assign_pattern_dimension(copy,"pattern:count:0",2.5);});
    rejects([&]{document::assign_pattern_dimension(copy,"pattern:spacing:0",0);});
    copy.value_locks.insert("pattern:spacing:0");
    rejects([&]{document::assign_pattern_dimension(copy,"pattern:spacing:0",12);});
    copy.pattern->circular=true;copy.pattern->count=6;
    dimensions=document::pattern_dimensions("pattern",copy);
    check(dimensions.size()==2&&dimensions[0].value==60&&!dimensions[0].driving);
    rejects([&]{document::assign_pattern_dimension(copy,"pattern:angle",45);});
    copy.pattern->full_circle=false;
    check(document::assign_pattern_dimension(copy,"pattern:angle",45));
    check(kernel::validated_pattern(*copy.pattern).angle_degrees==45);
    document::HistoryContainer feature;feature.id="pattern";feature.name="Pattern";
    feature.feature_kind=document::FeatureKind::DerivedCopy;feature.derived_copy=copy;
    std::vector<document::DimensionParameter> parameters;document::append_dimension_parameters(parameters,feature);
    document::DimensionIdentifiers identifiers;identifiers.synchronize(parameters);
    const auto angle=identifiers.identifier("pattern","parameter:pattern:angle");
    check(!angle.empty());
    identifiers.synchronize(parameters);check(identifiers.identifier("pattern","parameter:pattern:angle")==angle);
    check(document::DimensionIdentifiers::from_serialized(identifiers.serialized()).identifier("pattern","parameter:pattern:angle")==angle);
    std::cout<<"Pattern annotations, owning identities, scalar validation and identifiers passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
