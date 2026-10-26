#include "profile_solid_fixture.hpp"
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <set>
#include <tuple>
using namespace zima;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct HistoryTrace {
    std::ostringstream log;std::streambuf* old=std::cerr.rdbuf(log.rdbuf());
    std::string flag_before=std::getenv("ZIMA_CPP_PROFILE_COMMAND")?std::getenv("ZIMA_CPP_PROFILE_COMMAND"):"";
    static void flag(const std::string& value) {
#ifdef _WIN32
        _putenv_s("ZIMA_CPP_PROFILE_COMMAND",value.c_str());
#else
        if(value.empty())unsetenv("ZIMA_CPP_PROFILE_COMMAND");else setenv("ZIMA_CPP_PROFILE_COMMAND",value.c_str(),1);
#endif
    }
    HistoryTrace(){flag("1");}~HistoryTrace(){std::cerr.rdbuf(old);flag(flag_before);}
};
static void verify_long_suffix_reference_eviction() {
    auto doc=document::PartDocument::create_default();doc.history={test::rectangular_feature(doc,{20,20,10})};
    auto cut=test::rectangular_feature(doc,{2,2,15});cut.combine_mode=document::CombineMode::Subtract;doc.history.push_back(cut);
    for(int index=0;index<10;++index) {
        auto next=test::rectangular_feature(doc,{1,1,2});next.placement.x=30+3*index;doc.history.push_back(next);
    }
    doc.resolve_constructions();kernel::OcctKernel initial;
    const auto calculated=workspace::calculate_part(initial,doc);
    std::vector<kernel::BodyResult> previous;static_cast<void>(document::PartDocument::from_serialized(doc.serialized(calculated),&previous));
    kernel::OcctKernel editing;
    for(double length:{1.5,2.,2.5}) {
        auto next=doc;next.history[2].extrusion.length_forward=length;
        std::vector<kernel::BodyResult> result;
        {HistoryTrace trace;result=workspace::calculate_part(editing,next,&previous);
            require(trace.log.str().find("operations=12 reused=2 calculated=10")!=std::string::npos,
                "A long suffix evicted the input reference packet and recalculated its unchanged prefix");}
        kernel::OcctKernel oracle;const auto expected=workspace::calculate_part(oracle,next);
        require(std::abs(result.back().volume-(3978+2*length))<1e-7&&
            result.back().volume==expected.back().volume&&result.back().surface_area==expected.back().surface_area&&
            document::serialize_viewer_reference_geometry(result.back().mesh.original_references)==
                document::serialize_viewer_reference_geometry(expected.back().mesh.original_references),
            "Reference-packet restoration changed independent volume or exact source geometry");
        previous=std::move(result);
    }
    std::cout<<"Long suffix continuation: cold and repeated prefix reuse after bounded reference-packet eviction, exact oracle and independent volumes passed\n";
}
static void verify_live_regeneration(const kernel::OcctKernel& kernel) {
    verify_long_suffix_reference_eviction();
    auto doc=document::PartDocument::create_default();auto box=test::rectangular_feature(doc,{10,10,10});
    doc.history={box};const auto input=workspace::calculate_part(kernel,doc);
    auto fillet=document::PartDocument::create_fillet_container({input.back().mesh.edges.front().reference});
    fillet.edge_treatment.primary_size=.5;doc.history.push_back(fillet);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));
    for(const auto& feature:doc.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    doc.set_body_history(graph);const auto original=workspace::calculate_part(kernel,doc);
    require(kernel.has_current_calculation(original),"Live calculation lost its runtime proof");
    kernel::OcctKernel another;
    require(!another.has_current_calculation(original),"A different kernel accepted another runtime's proof");
    std::vector<kernel::BodyResult> loaded;
    doc=document::PartDocument::from_serialized(doc.serialized(original),&loaded);
    require(!kernel.has_current_calculation(loaded),"Native loading restored an ephemeral runtime proof");
    workspace::Workspace live;const auto id=doc.document_id;live.add_part(doc,std::move(loaded));
    {HistoryTrace trace;static_cast<void>(workspace::regenerate_part(live,kernel,id));
        require(trace.log.str().find("operations=2 reused=0 calculated=2")!=std::string::npos,"First regeneration reused an unverified native snapshot");}
    auto& session=live.open_part(id)->session;const auto before=session.document().serialized(session.calculated_boundaries());
    {HistoryTrace trace;static_cast<void>(workspace::regenerate_part(live,kernel,id));
        require(trace.log.str().find("operations=2 reused=2 calculated=0")!=std::string::npos,"Repeated regeneration recalculated unchanged live geometry");}
    require(before==session.document().serialized(session.calculated_boundaries()),"Live regeneration changed persisted geometry/properties/references");
    {HistoryTrace trace;workspace::delete_part_history(live,id,kernel,fillet.id);
        require(trace.log.str().find("operations=1 reused=1 calculated=0")!=std::string::npos,"Deleting the last Fillet recalculated its unchanged input");}
    auto measured=session.calculated_boundaries().back();kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-1000)<1e-8,"Deletion retained rounded geometry");
    std::vector<kernel::BodyResult> reopened;
    static_cast<void>(document::PartDocument::from_serialized(session.document().serialized(session.calculated_boundaries()),&reopened));
    kernel::OcctKernel fresh;fresh.ensure_body_properties(reopened.back());
    require(std::abs(reopened.back().volume-1000)<1e-8,"Deleted history lost its real analysis solid on reopen");
    require(workspace::step_part_document_history(live,id,false)&&session.document().find_container(fillet.id),"Deletion Undo lost the Fillet");
    require(workspace::step_part_document_history(live,id,true)&&!session.document().find_container(fillet.id),"Deletion Redo failed");
    auto changed=session.document();changed.find_container(box.id)->extrusion.length_forward=6;
    session.commit(changed,session.calculated_boundaries());
    static_cast<void>(workspace::regenerate_part(live,kernel,id));
    require(std::abs(session.calculated_boundaries().back().volume-1200)<1e-8,"Changed source reused obsolete live geometry");
}
static void verify_cold_history_resume() {
    kernel::OcctKernel original_kernel;
    auto doc=document::PartDocument::create_default();auto box=test::rectangular_feature(doc,{10,10,10});
    doc.history={box};const auto input=workspace::calculate_part(original_kernel,doc);
    auto fillet=document::PartDocument::create_fillet_container({input.back().mesh.edges.front().reference});
    fillet.edge_treatment.primary_size=.5;doc.history.push_back(fillet);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Cold native continuation"));
    for(const auto& feature:doc.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    doc.set_body_history(graph);const auto calculated=workspace::calculate_part(original_kernel,doc);
    std::vector<kernel::BodyResult> loaded;
    doc=document::PartDocument::from_serialized(doc.serialized(calculated),&loaded);
    const auto authored=doc.serialized();
    kernel::OcctKernel resumed_kernel;
    require(!resumed_kernel.has_current_calculation(loaded),"Loading restored a live calculation proof");
    const auto identities=[](const kernel::ViewerReferenceGeometry& geometry) {
        std::set<std::tuple<int,std::string,std::string,std::string>> result;
        for(const auto& ref:geometry.triangle_references)result.emplace(0,ref.owner_id,ref.semantic_key,ref.instance_path);
        for(const auto& edge:geometry.edges)result.emplace(1,edge.reference.owner_id,edge.reference.semantic_key,edge.reference.instance_path);
        for(const auto& point:geometry.points)result.emplace(2,point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path);
        return result;
    };
    for(double radius:{.75,.6,.9}) {
        auto changed=doc;changed.find_container(fillet.id)->edge_treatment.primary_size=radius;
        std::vector<kernel::BodyResult> resumed;
        {HistoryTrace trace;resumed=workspace::calculate_part(resumed_kernel,changed,&loaded);
            require(trace.log.str().find("operations=2 reused=1 calculated=1")!=std::string::npos,
                "Cold/repeated tail edit recalculated its unchanged native prefix");}
        kernel::OcctKernel fresh;auto expected=workspace::calculate_part(fresh,changed);
        auto actual_body=resumed.back(),expected_body=expected.back();
        resumed_kernel.ensure_body_properties(actual_body);fresh.ensure_body_properties(expected_body);
        require(std::abs(actual_body.volume-expected_body.volume)<1e-8&&
            std::abs(actual_body.surface_area-expected_body.surface_area)<1e-7,"Resumed history differs from a fresh geometric calculation");
        require(identities(actual_body.mesh.original_references)==identities(expected_body.mesh.original_references),
            "Native continuation changed original face/edge/point identities");
        require(resumed_kernel.has_current_calculation(resumed),"A completed incremental edit lost its current calculation proof");
        require(doc.serialized()==authored,"Native continuation changed its read-only input document");
        std::vector<kernel::BodyResult> reopened;
        static_cast<void>(document::PartDocument::from_serialized(changed.serialized(resumed),&reopened));
        loaded=std::move(resumed);
    }
    auto changed=doc;changed.find_container(fillet.id)->edge_treatment.primary_size=.8;
    auto damaged=calculated;
    for(auto& packet:damaged) {
        packet.incremental_history.reset();
        for(auto& [id,rows]:packet.body_boundaries) {
            auto copy=rows.get();for(auto& row:copy)row.incremental_history.reset();rows=std::move(copy);
        }
    }
    kernel::OcctKernel missing;
    {HistoryTrace trace;static_cast<void>(workspace::calculate_part(missing,changed,&damaged));
        require(trace.log.str().find("operations=2 reused=0 calculated=2")!=std::string::npos,
            "Missing optional continuation data reused unverified ancestry");}
    auto compatible=calculated;
    const auto set_algorithm=[](kernel::BodyResult& packet,const std::string& algorithm) {
        if(packet.incremental_history) {
            auto copy=std::make_shared<kernel::KernelHistoryCache>(*packet.incremental_history);
            copy->algorithm=algorithm;packet.incremental_history=std::move(copy);
        }
    };
    for(auto& packet:compatible) {
        set_algorithm(packet,"history-v1/compatible-rebuild");
        for(auto& [id,rows]:packet.body_boundaries) {
            auto copy=rows.get();for(auto& row:copy)set_algorithm(row,"history-v1/compatible-rebuild");rows=std::move(copy);
        }
    }
    kernel::OcctKernel compatible_kernel,fresh_compatible_kernel;
    std::vector<kernel::BodyResult> compatible_result;
    {HistoryTrace trace;compatible_result=workspace::calculate_part(compatible_kernel,changed,&compatible);
        require(trace.log.str().find("operations=2 reused=1 calculated=1")!=std::string::npos,
            "A compatible history-v1 producer rebuild discarded the saved prefix");}
    auto compatible_expected=workspace::calculate_part(fresh_compatible_kernel,changed);
    auto compatible_body=compatible_result.back(),compatible_expected_body=compatible_expected.back();
    compatible_kernel.ensure_body_properties(compatible_body);
    fresh_compatible_kernel.ensure_body_properties(compatible_expected_body);
    require(std::abs(compatible_body.volume-compatible_expected_body.volume)<1e-8&&
        std::abs(compatible_body.surface_area-compatible_expected_body.surface_area)<1e-7&&
        identities(compatible_body.mesh.original_references)==identities(compatible_expected_body.mesh.original_references),
        "Compatible history-v1 continuation differs from a fresh calculation");
    auto stale=calculated;
    const auto invalidate=[](kernel::BodyResult& packet) {
        if(packet.incremental_history) {
            auto copy=std::make_shared<kernel::KernelHistoryCache>(*packet.incremental_history);
            copy->algorithm="different-modeling-algorithm";packet.incremental_history=std::move(copy);
        }
    };
    for(auto& packet:stale) {
        invalidate(packet);
        for(auto& [id,rows]:packet.body_boundaries) {
            auto copy=rows.get();for(auto& row:copy)invalidate(row);rows=std::move(copy);
        }
    }
    kernel::OcctKernel changed_algorithm;
    {HistoryTrace trace;static_cast<void>(workspace::calculate_part(changed_algorithm,changed,&stale));
        require(trace.log.str().find("operations=2 reused=0 calculated=2")!=std::string::npos,
            "A different algorithm reused saved native continuation data");}
    auto invalid_packet=document::serialize_body_result(calculated.back());
    invalid_packet["incremental_history"]={{"algorithm",17},{"frames",false}};
    const auto ignored=document::load_body_result(invalid_packet);
    require(!ignored.incremental_history&&ignored.mesh.vertices==calculated.back().mesh.vertices&&
        ignored.mesh.triangles==calculated.back().mesh.triangles,"Invalid optional continuation metadata hid valid display geometry");
    std::cout<<"Cold native continuation: first/repeated tail edits, fresh geometry/identities, native reopen, input immutability, stale/missing cache and invalid optional metadata passed\n";
}

static void verify_recovery_prefix() {
    auto doc=document::PartDocument::create_default();
    auto first=test::rectangular_feature(doc,{10,10,10});
    auto second=test::rectangular_feature(doc,{7,8,9});second.placement.x=20;
    doc.history={first,second};doc.resolve_constructions();auto operations=doc.kernel_operations();
    for(auto& operation:operations)operation.body={};
    require(operations.size()==2,"Recovery fixture operation count changed");
    kernel::OcctKernel initial;auto previous=initial.evaluate_history(operations);
    for(auto& boundary:previous)boundary=document::load_body_result(document::serialize_body_result(boundary));
    auto broken=operations.back();broken.owner_id="intentional-failed-suffix";broken.input_error="Intentional recovery fixture failure";
    operations.push_back(broken);kernel::OcctKernel resumed,fresh;
    std::vector<kernel::BodyResult> recovered;
    {HistoryTrace trace;recovered=resumed.evaluate_history_recovering(operations,previous);
        require(trace.log.str().find("[history recovery] unchanged_prefix=2")!=std::string::npos&&
            trace.log.str().find("operations=1 ")==std::string::npos,
            "Suffix failure recovery walked the known valid prefix boundary by boundary");}
    const auto expected=fresh.evaluate_history_recovering(operations);
    require(recovered.size()==expected.size()&&recovered.back().calculation_errors==expected.back().calculation_errors,
        "Prefix recovery changed failure attribution");
    const auto canonical=[](const kernel::ViewerReferenceGeometry& geometry) {
        using Point=std::array<std::uint64_t,3>;using Triangle=std::array<Point,3>;
        const auto bits=[](const kernel::Vec3& p){return Point{std::bit_cast<std::uint64_t>(p.x),std::bit_cast<std::uint64_t>(p.y),std::bit_cast<std::uint64_t>(p.z)};};
        std::set<Point> vertices;for(const auto& point:geometry.vertices)vertices.insert(bits(point));
        std::vector<std::tuple<std::string,std::string,std::string,Triangle>> triangles;
        for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
            Triangle points{bits(geometry.vertices.at(geometry.triangles.at(i*3))),
                bits(geometry.vertices.at(geometry.triangles.at(i*3+1))),bits(geometry.vertices.at(geometry.triangles.at(i*3+2)))};
            auto best=points;std::rotate(points.begin(),points.begin()+1,points.end());best=std::min(best,points);
            std::rotate(points.begin(),points.begin()+1,points.end());best=std::min(best,points);
            const auto& ref=geometry.triangle_references[i];triangles.emplace_back(ref.owner_id,ref.semantic_key,ref.instance_path,best);
        }
        std::sort(triangles.begin(),triangles.end());return std::pair{vertices,triangles};
    };
    for(std::size_t i=0;i<recovered.size();++i) {
        auto actual=recovered[i],baseline=expected[i];resumed.ensure_body_properties(actual);fresh.ensure_body_properties(baseline);
        require(std::abs(actual.volume-baseline.volume)<1e-9&&std::abs(actual.surface_area-baseline.surface_area)<1e-9,
            "Prefix recovery changed retained geometry");
        auto actual_references=document::serialize_body_result(actual).at("original_references");
        auto baseline_references=document::serialize_body_result(baseline).at("original_references");
        // OCCT may enumerate mesh vertices differently after native reload.
        // Compare exact bits and oriented triangles, never rounded positions.
        for(auto* packet:{&actual_references,&baseline_references}){packet->erase("vertices_binary");packet->erase("triangles_binary");}
        require(actual_references==baseline_references&&canonical(actual.mesh.original_references)==canonical(baseline.mesh.original_references),
            "Prefix recovery changed original geometry/side identities");
    }
    operations.front().input_error="Changed earlier input";kernel::OcctKernel changed;
    const auto rejected=changed.evaluate_history_recovering(operations,previous);
    require(rejected.front().calculation_errors.contains(operations.front().owner_id)&&rejected.back().mesh.triangles.empty(),
        "Recovery retained stale material after its earliest input changed");
    std::cout<<"Recovery prefix: cold saved input, exact fresh geometry/references/errors and earlier-source invalidation passed\n";
}

static void verify_sheet_suppression(const kernel::OcctKernel& kernel) {
    auto doc=document::PartDocument::create_default();
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=bend.id;
    static_cast<void>(sketch.add_segment(0,0,40,0));bend.bend.sketch_id=sketch.id;
    bend.bend.thickness_override=true;bend.bend.thickness=2;
    bend.bend.radius_follows_thickness=false;bend.bend.radius=8;bend.bend.angle_degrees=77;
    auto unbend=document::PartDocument::create_sketch_container();unbend.feature_kind=document::FeatureKind::Unbend;unbend.sheet_state.all=true;
    auto formed=document::PartDocument::create_sketch_container();formed.feature_kind=document::FeatureKind::BendBack;formed.sheet_state.all=true;
    doc.history={bend,unbend,formed};doc.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Sheet"));
    for(const auto& feature:doc.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    doc.set_body_history(graph);doc.resolve_constructions();const auto original=doc.serialized();
    workspace::Workspace live;const auto id=doc.document_id;
    live.add_part(doc,workspace::calculate_part(kernel,doc));
    require(workspace::set_part_history_suppressed(live,id,kernel,unbend.id,true),"Unbend suppression did not change history");
    auto& session=live.open_part(id)->session;
    require(!session.document().find_container(bend.id)->suppressed&&session.document().find_container(unbend.id)->suppressed&&
        session.document().find_container(formed.id)->suppressed,"Unbend suppression omitted Bend Back or changed its source");
    require(workspace::set_part_history_suppressed(live,id,kernel,formed.id,false)&&session.document().serialized()==original,
        "Bend Back restoration did not restore preceding Unbend");
    require(workspace::set_part_history_suppressed(live,id,kernel,bend.id,true),"Sheet source suppression did not change history");
    for(const auto& feature:doc.history)require(session.document().find_container(feature.id)->suppressed,"Sheet source suppression omitted a state");
    require(workspace::set_part_history_suppressed(live,id,kernel,formed.id,false)&&session.document().serialized()==original&&
        session.calculated_boundaries().back().calculation_errors.empty(),"Bend Back restoration omitted its original sheet source");
}
int main() {
    try {
        kernel::OcctKernel kernel;
        verify_live_regeneration(kernel);verify_cold_history_resume();verify_recovery_prefix();
        verify_sheet_suppression(kernel);
        auto doc=document::PartDocument::create_default();
        auto source=test::rectangular_feature(doc,{10,10,10});
        doc.history={source};
        const auto geometry=workspace::calculate_part(kernel,doc);
        require(!geometry.empty()&&!geometry.back().mesh.edges.empty(),"Fixture has no edges");
        const auto reference=geometry.back().mesh.edges.front().reference;
        auto fillet=document::PartDocument::create_fillet_container({reference});
        fillet.edge_treatment.primary_size=.5;
        doc.history.push_back(fillet);
        const auto rounded=workspace::calculate_part(kernel,doc);
        require(rounded.back().calculation_errors.empty(),"Fillet fixture failed calculation");
        auto chamfer=document::PartDocument::create_chamfer_container({reference});
        chamfer.edge_treatment.primary_size=.1;
        doc.history.push_back(chamfer);
        bool valid_chamfer=false;
        for(const auto& edge:rounded.back().mesh.edges)if(edge.reference.owner_id==fillet.id) {
            chamfer.edge_treatment.routes={{edge.reference}};doc.history.back()=chamfer;
            if(workspace::calculate_part(kernel,doc).back().calculation_errors.empty()){valid_chamfer=true;break;}
        }
        require(valid_chamfer,"Fillet fixture has no usable dependent Chamfer edge");
        document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Body"));
        for(const auto& f:doc.history)graph.insert({document::PartHistoryKind::Feature,f.id});
        doc.set_body_history(graph);
        require(workspace::calculate_part(kernel,doc).back().calculation_errors.empty(),"Dependent Chamfer fixture failed calculation");
        // Workspace insertion resolves owned origins. Capture the baseline
        // after the same initialization, including exact signed Euler zeros.
        doc.resolve_constructions();
        const auto original=doc.serialized();
        const auto plan=workspace::plan_history_deletion(doc,source.id);
        require(plan.affected==std::set<std::string>{fillet.id,chamfer.id},"Dependency closure lost descendants");
        {
            auto suppression_doc=doc;
            auto independent=test::rectangular_feature(suppression_doc,{3,3,3});
            suppression_doc.history.push_back(independent);
            auto suppression_graph=suppression_doc.body_history;
            const auto source_body=suppression_graph.active_body_id();
            static_cast<void>(suppression_graph.create_body("Independent"));
            suppression_graph.insert({document::PartHistoryKind::Feature,independent.id});
            suppression_graph.activate(source_body);suppression_doc.set_body_history(suppression_graph);
            suppression_doc.resolve_constructions();
            const auto unchanged=suppression_doc.serialized();
            workspace::Workspace suppression_live;
            const auto suppression_id=suppression_doc.document_id;
            suppression_live.add_part(suppression_doc,workspace::calculate_part(kernel,suppression_doc));
            auto& session=suppression_live.open_part(suppression_id)->session;
            const auto revision=session.revision();
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,true),"Source suppression did not change history");
            require(session.revision()==revision+1&&session.document().find_container(source.id)->suppressed&&
                session.document().find_container(fillet.id)->suppressed&&session.document().find_container(chamfer.id)->suppressed&&
                !session.document().find_container(independent.id)->suppressed,"Suppression lost transitive consumers or changed independent history");
            require(session.calculated_boundaries().back().calculation_errors.empty()&&session.document().removed_reference_states.empty(),
                "Suppression reported deleted/missing references");
            auto expected=suppression_doc;
            for(const auto& id:{source.id,fillet.id,chamfer.id})expected.find_container(id)->suppressed=true;
            require(session.document().serialized()==expected.serialized(),"Suppression rewrote authored definitions or references");
            const auto generation=session.data_generation();
            require(!workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,true)&&
                session.revision()==revision+1&&session.data_generation()==generation,"Repeated suppression recalculated or committed");
            std::vector<kernel::BodyResult> saved;
            const auto reopened=document::PartDocument::from_serialized(session.document().serialized(session.calculated_boundaries()),&saved);
            require(reopened.serialized()==expected.serialized()&&!saved.empty(),"Native reopen lost cascaded suppression");
            require(workspace::step_part_document_history(suppression_live,suppression_id,false)&&session.document().serialized()==unchanged,
                "Suppression Undo did not restore source and all consumers together");
            require(workspace::step_part_document_history(suppression_live,suppression_id,true)&&session.document().serialized()==expected.serialized(),
                "Suppression Redo did not restore all flags together");
            const auto restore_revision=session.revision();
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,fillet.id,false),"Dependent restoration did not change history");
            auto partially_restored=expected;
            partially_restored.find_container(source.id)->suppressed=false;
            partially_restored.find_container(fillet.id)->suppressed=false;
            require(session.revision()==restore_revision+1&&session.document().serialized()==partially_restored.serialized()&&
                session.calculated_boundaries().back().calculation_errors.empty(),"Restoration omitted prerequisite sources or enabled downstream history");
            require(workspace::step_part_document_history(suppression_live,suppression_id,false)&&session.document().serialized()==expected.serialized(),
                "Restoration Undo did not return all flags together");
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,false)&&
                !session.document().find_container(source.id)->suppressed&&session.document().find_container(fillet.id)->suppressed&&
                session.document().find_container(chamfer.id)->suppressed,"Source restoration enabled its downstream consumers");
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,chamfer.id,false)&&
                session.document().serialized()==unchanged&&session.calculated_boundaries().back().calculation_errors.empty(),
                "Restoring a final dependent did not enable its transitive prerequisites");
        }
        require(doc.serialized()==original,"Preview mutated document");
        workspace::Workspace live;const auto id=doc.document_id;
        live.add_part(doc,workspace::calculate_part(kernel,doc));
        const auto revision=live.open_part(id)->session.revision();
        workspace::delete_part_history(live,id,kernel,source.id);
        auto& session=live.open_part(id)->session;
        require(session.revision()==revision+1,"Deletion did not commit exactly once");
        const auto& deleted=session.document();
        require(!deleted.find_container(source.id)&&deleted.find_container(fillet.id)&&deleted.find_container(chamfer.id),"Deletion removed dependent history");
        require(deleted.find_container(fillet.id)->edge_treatment.flattened_edges().empty(),"Deleted source edge retained");
        require(deleted.find_container(chamfer.id)->edge_treatment.flattened_edges().empty(),"Invalid dependent edge retained");
        require(deleted.removed_reference_states.size()==2,"Missing-reference states disappeared");
        auto loaded=document::PartDocument::from_serialized(deleted.serialized());
        require(loaded.removed_reference_states==deleted.removed_reference_states,"Incomplete state did not round trip");
        auto regenerated=workspace::calculate_part_with_resolved_references(kernel,loaded);
        require(loaded.removed_reference_states.size()==2,"Regeneration acknowledged a missing reference");
        require(regenerated.back().calculation_errors.contains(fillet.id),"Missing reference calculated geometry");
        require(workspace::step_part_document_history(live,id,false),"Undo unavailable");
        require(session.document().serialized()==original,"Undo did not restore all references");
        require(workspace::step_part_document_history(live,id,true),"Redo unavailable");
        auto numeric=session.document();numeric.find_container(fillet.id)->edge_treatment.primary_size=1;
        require(!workspace::refresh_removed_reference_states(numeric),"Numeric edit acknowledged missing references");
        auto repaired=session.document();repaired.find_container(fillet.id)->edge_treatment.routes={{reference}};
        require(workspace::refresh_removed_reference_states(repaired)&&!repaired.removed_reference_states.contains(fillet.id),"Replacement references did not acknowledge repair");
        auto repair_doc=session.document();
        auto replacement=test::rectangular_feature(repair_doc,{12,12,12});
        auto independent=test::rectangular_feature(repair_doc,{3,3,3});
        repair_doc.history.insert(repair_doc.history.begin(),replacement);repair_doc.history.push_back(independent);
        document::BodyHistoryGraph repair_graph;const auto repair_body=repair_graph.create_body("Repair");
        for(const auto& owner:{replacement.id,fillet.id,chamfer.id})repair_graph.insert({document::PartHistoryKind::Feature,owner});
        const auto independent_body=repair_graph.create_body("Independent");
        repair_graph.insert({document::PartHistoryKind::Feature,independent.id});repair_graph.activate(repair_body);
        repair_doc.set_body_history(repair_graph);
        auto repair_geometry=workspace::calculate_part_with_resolved_references(kernel,repair_doc);
        require(repair_geometry.back().body_outputs.at(independent_body)->calculation_errors.empty(),"Broken references blocked an independent Body");
        auto edited=*repair_doc.find_container(fillet.id);
        edited.edge_treatment.routes={{repair_geometry.front().mesh.edges.front().reference}};
        workspace::commit_part_document(live,id,repair_doc,repair_geometry);
        require(workspace::commit_edge_treatment(live,kernel,id,edited,workspace::EdgeTreatmentEditMode::Replace),"Reference repair did not commit");
        require(!session.document().removed_reference_states.contains(fillet.id)&&session.document().removed_reference_states.contains(chamfer.id),"Repair acknowledged other broken features");
        require(!session.calculated_boundaries().back().calculation_errors.contains(fillet.id),"Repaired fillet failed calculation");
        require(workspace::step_part_document_history(live,id,false)&&session.document().removed_reference_states.contains(fillet.id),"Repair Undo lost incomplete state");
        auto pattern_doc=document::PartDocument::create_default();
        auto pattern_source=test::rectangular_feature(pattern_doc,{4,4,4});
        auto pattern=document::PartDocument::create_fillet_container({reference});
        pattern.feature_kind=document::FeatureKind::DerivedCopy;pattern.name="Pattern";
        pattern.derived_copy.source_id=pattern_source.id;pattern.derived_copy.pattern=kernel::PatternRequest{};
        pattern_doc.history={pattern_source,pattern};
        document::BodyHistoryGraph pattern_graph;static_cast<void>(pattern_graph.create_body("Pattern"));
        for(const auto& f:pattern_doc.history)pattern_graph.insert({document::PartHistoryKind::Feature,f.id});
        pattern_doc.set_body_history(pattern_graph);
        workspace::Workspace pattern_live;const auto pattern_id=pattern_doc.document_id;
        pattern_live.add_part(pattern_doc,workspace::calculate_part(kernel,pattern_doc));
        workspace::delete_part_history(pattern_live,pattern_id,kernel,pattern_source.id);
        const auto pattern_roundtrip=document::PartDocument::from_serialized(pattern_live.open_part(pattern_id)->session.document().serialized());
        require(pattern_roundtrip.find_container(pattern.id)&&pattern_roundtrip.find_container(pattern.id)->derived_copy.source_id.empty()&&
            pattern_roundtrip.removed_reference_states.contains(pattern.id),"Pattern without source could not remain in native history");
        std::cout<<"History deletion contracts passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
