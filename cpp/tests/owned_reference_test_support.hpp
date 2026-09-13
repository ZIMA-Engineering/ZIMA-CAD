#pragma once
#include "context_reference_test_support.hpp"
#include "sweep_test_support.hpp"
namespace zima::test_support {
// A valid calculated native Helical Sweep with a reference only in its owned
// base Sketch. No root Sketch can accidentally satisfy regeneration's scan.
inline void install_owned_helical_reference(ContextReferenceFixture& fixture,const kernel::OcctKernel& kernel) {
    auto feature=sweep_fixture(document::FeatureKind::HelicalSweep);
    auto base=sketcher::Sketch::from_serialized(feature.helical.sketches[0]);
    base.add_external_reference(fixture.target.sketches.front().external_references.front());
    feature.helical.sketches[0]=base.serialized();fixture.sketch_id=base.id;
    fixture.target.history={feature};fixture.target.sketches.clear();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Helical Body"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});fixture.target.set_body_history(std::move(graph));
    auto calculated=kernel.evaluate_history(fixture.target.kernel_operations());
    fixture.target.save(fixture.directory/"context-target.prtz",calculated);
    fixture.inner.cuts.clear();fixture.inner.sketches.clear();fixture.inner.components.back().calculated_source=calculated.back();
    fixture.inner.save(fixture.directory/"context-inner.asmz");
    for(auto& occurrence:fixture.top.components){
        auto updated=assembly::AssemblyDocument::create_assembly_occurrence(occurrence.name,fixture.inner.document_id,"context-inner.asmz",fixture.inner);
        updated.occurrence_id=occurrence.occurrence_id;updated.placement=occurrence.placement;occurrence=std::move(updated);
    }
    fixture.top.save(fixture.directory/"context-top.asmz");
}
}
