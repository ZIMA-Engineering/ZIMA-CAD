#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/stable_id.hpp>
namespace zima::test {
struct CopyQueryFixture {
    document::PartDocument document=document::PartDocument::create_default();
    std::string source,other,mirror,pattern,combined,copy_of_boolean;
};
inline CopyQueryFixture copy_query_fixture() {
    CopyQueryFixture fixture;
    auto a=document::PartDocument::create_box_container();a.box={6,4,2};a.placement.x=20;
    auto b=document::PartDocument::create_box_container();b.box={3,2,2};b.placement.x=50;
    fixture.document.history={a,b};document::BodyHistoryGraph graph;
    fixture.source=graph.create_body("Zdroj");graph.insert({document::PartHistoryKind::Feature,a.id});
    fixture.other=graph.create_body("Jiný zdroj");graph.insert({document::PartHistoryKind::Feature,b.id});
    const auto copy=[&](const std::string& source,bool pattern) {
        document::BodyHistory body;body.scope.id=kernel::make_stable_id();body.name=pattern?"Pole žluťoučké":"Zrcadlo";
        body.scope.placement.x=-2;body.scope.placement.value_locks.insert("x");
        body.derived_copy=document::DerivedCopyParameters{source,{{},body.origin().id,pattern?"origin:axis:z":"origin:plane:yz"}};
        if(pattern) {
            body.derived_copy->pattern=kernel::PatternRequest{};
            auto& p=*body.derived_copy->pattern;
            p.linear[0].count=3;p.linear[0].spacing=30;p.linear[0].distribution=kernel::PatternDistribution::Symmetric;
            p.linear[1].local_axis=1;p.linear[1].count=2;p.linear[1].spacing=20;p.linear[1].distribution=kernel::PatternDistribution::Both;p.linear[1].reverse_count=2;
            body.derived_copy->value_locks.insert("pattern:spacing:1");
        }
        document::PartDocument::resolve_copy_reference(*body.derived_copy,body.scope.id,body.scope.placement,{});
        return graph.create_derived_copy(std::move(body));
    };
    fixture.mirror=copy(fixture.source,false);fixture.pattern=copy(fixture.source,true);
    fixture.combined=graph.create_boolean("Součet",kernel::BodyCombination::Add,fixture.source,fixture.other);
    fixture.copy_of_boolean=copy(fixture.combined,false);graph.activate({});fixture.document.set_body_history(std::move(graph));
    return fixture;
}
} // namespace zima::test
