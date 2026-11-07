#include "profile_solid_fixture.hpp"
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
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
static bool owns_persisted_history_cache(
        const std::vector<kernel::BodyResult>& calculated) {
    const auto packet_owns_cache=[](auto&& self,
            const kernel::BodyResult& packet)->bool {
        if(packet.incremental_history)return true;
        for(const auto& [id,rows]:packet.body_boundaries) {
            static_cast<void>(id);
            if(std::ranges::any_of(rows,[&](const auto& row) {
                    return self(self,row);
                }))return true;
        }
        return false;
    };
    return std::ranges::any_of(calculated,[&](const auto& packet) {
        return packet_owns_cache(packet_owns_cache,packet);
    });
}
static void verify_long_suffix_cacheless_regeneration() {
    auto doc=document::PartDocument::create_default();doc.history={test::rectangular_feature(doc,{20,20,10})};
    auto cut=test::rectangular_feature(doc,{2,2,15});cut.combine_mode=document::CombineMode::Subtract;doc.history.push_back(cut);
    for(int index=0;index<10;++index) {
        auto next=test::rectangular_feature(doc,{1,1,2});next.placement.x=30+3*index;doc.history.push_back(next);
    }
    doc.resolve_constructions();kernel::OcctKernel initial;
    const auto calculated=workspace::calculate_part(initial,doc);
    require(!owns_persisted_history_cache(calculated),
        "Final-first Part calculation retained a persistent per-step B-Rep cache");
    std::vector<kernel::BodyResult> previous;static_cast<void>(document::PartDocument::from_serialized(doc.serialized(calculated),&previous));
    require(!owns_persisted_history_cache(previous),
        "Native reopen restored a removed per-step B-Rep cache");
    kernel::OcctKernel editing;
    std::size_t edit_index{};
    for(double length:{1.5,2.,2.5}) {
        auto next=doc;next.history[2].extrusion.length_forward=length;
        std::vector<kernel::BodyResult> result;
        {HistoryTrace trace;result=workspace::calculate_part(editing,next,&previous);
            const auto expected=edit_index==0
                ?"operations=12 reused=0 calculated=12"
                :"operations=12 reused=2 calculated=10";
            require(trace.log.str().find(expected)!=std::string::npos,
                edit_index==0
                    ?"Cold edit reused an unpersisted per-step B-Rep cache"
                    :"Repeated live edit recalculated its unchanged in-memory prefix");}
        require(!owns_persisted_history_cache(result),
            "Explicit edit reintroduced a persistent per-step B-Rep cache");
        kernel::OcctKernel oracle;const auto expected=workspace::calculate_part(oracle,next);
        auto measured=result.back(),expected_measured=expected.back();
        editing.ensure_body_properties(measured);oracle.ensure_body_properties(expected_measured);
        require(std::abs(measured.volume-(3978+2*length))<1e-7&&
            measured.volume==expected_measured.volume&&measured.surface_area==expected_measured.surface_area&&
            document::serialize_viewer_reference_geometry(result.back().mesh.original_references)==
                document::serialize_viewer_reference_geometry(expected.back().mesh.original_references),
            "Reference-packet restoration changed independent volume or exact source geometry");
        previous=std::move(result);
        ++edit_index;
    }
    std::cout<<"Cacheless long suffix: cold recomputation, transient live prefix reuse, exact oracle and independent volumes passed\n";
}
static void verify_live_regeneration(const kernel::OcctKernel& kernel) {
    verify_long_suffix_cacheless_regeneration();
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
    auto changed_result=session.calculated_boundaries().back();kernel.ensure_body_properties(changed_result);
    require(std::abs(changed_result.volume-1200)<1e-8,"Changed source reused obsolete live geometry");
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
    require(!owns_persisted_history_cache(calculated),
        "Ordinary Part calculation retained a persistent history cache");
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
    std::size_t edit_index{};
    for(double radius:{.75,.6,.9}) {
        auto changed=doc;changed.find_container(fillet.id)->edge_treatment.primary_size=radius;
        std::vector<kernel::BodyResult> resumed;
        {HistoryTrace trace;resumed=workspace::calculate_part(resumed_kernel,changed,&loaded);
            const auto expected=edit_index==0
                ?"operations=2 reused=0 calculated=2"
                :"operations=2 reused=1 calculated=1";
            require(trace.log.str().find(expected)!=std::string::npos,
                edit_index==0
                    ?"Cold tail edit reused removed persistent history state"
                    :"Repeated live tail edit recalculated its unchanged prefix");}
        require(!owns_persisted_history_cache(resumed),
            "Tail edit reintroduced persistent history state");
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
        require(!owns_persisted_history_cache(reopened),
            "Tail-edit reopen restored persistent history state");
        loaded=std::move(resumed);
        ++edit_index;
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
    auto invalid_packet=document::serialize_body_result(calculated.back());
    invalid_packet["incremental_history"]={{"algorithm",17},{"frames",false}};
    const auto ignored=document::load_body_result(invalid_packet);
    require(!ignored.incremental_history&&ignored.mesh.vertices==calculated.back().mesh.vertices&&
        ignored.mesh.triangles==calculated.back().mesh.triangles,"Invalid optional continuation metadata hid valid display geometry");
    std::cout<<"Cacheless native continuation: cold/repeated tail edits, fresh geometry/identities, reopen, input immutability and invalid optional metadata passed\n";
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
    auto surviving=rejected.back();changed.ensure_body_properties(surviving);
    kernel::OcctKernel independent;const auto without_cache=independent.evaluate_history_recovering(operations);
    require(rejected.front().calculation_errors.contains(operations.front().owner_id)&&
        rejected.back().calculation_errors.contains(operations.front().owner_id)&&
        std::abs(surviving.volume-7.*8.*9.)<1e-9&&
        canonical(rejected.back().mesh.original_references)==canonical(without_cache.back().mesh.original_references),
        "Recovery retained stale first-stock material or lost the independent valid second stock");
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
static void verify_retained_feature_and_sketch_frames(const kernel::OcctKernel& kernel) {
    auto part = document::PartDocument::create_default();
    const auto body = part.body_history.create_body("Retained frames");
    auto plane = document::PartDocument::create_construction(document::ConstructionKind::Plane);
    plane.origin = {11, -8, 17};
    plane.rotation = plane.absolute_rotation = {31, -23, 42};
    part.insert_history_entry(document::PartHistoryKind::Construction, plane.id);
    part.constructions.push_back(plane);
    auto dependent = test::rectangular_feature(part, {10, 6, 5});
    document::ConstructionReference support;
    support.owner_id = plane.entity_id;
    support.semantic_key = "plane";
    support.orientation_drives_rotation = true;
    support.orientation_role = "front";
    dependent.placement.references.push_back(support);
    part.insert_history_entry(document::PartHistoryKind::Feature, dependent.id);
    part.history.push_back(dependent);
    auto sketch = sketcher::Sketch::create_default();
    sketch.plane_reference_owner_id = plane.entity_id;
    static_cast<void>(sketch.add_segment(0, 0, 5, 2));
    part.insert_history_entry(document::PartHistoryKind::Sketch, sketch.id);
    part.sketches.push_back(sketch);
    auto calculated = workspace::calculate_part_with_resolved_references(kernel, part);
    require(calculated.back().calculation_errors.empty(), "Retained-frame fixture did not calculate");
    const auto volume = [&](const auto& boundaries) {
        require(!boundaries.empty() && boundaries.back().calculation_errors.empty(),
            "A retained-frame feature became a calculation error");
        const auto found = boundaries.back().body_outputs.find(body);
        require(found != boundaries.back().body_outputs.end() && found->second->has_solid(),
            "A retained-frame feature lost its owned solid");
        auto measured = found->second.get();
        kernel.ensure_body_properties(measured);
        return measured.volume;
    };
    const double saved_volume = volume(calculated);
    const auto frame = [](const sketcher::Sketch& value) {
        return std::array{value.world_point(0, 0), value.world_point(1, 0), value.world_point(0, 1)};
    };
    const auto saved_frame = frame(*std::ranges::find(part.sketches, sketch.id, &sketcher::Sketch::id));
    require(saved_frame[0] != kernel::Vec3{}, "Retained-frame fixture lost the translated Plane");
    auto expected_placement = part.find_container(dependent.id)->placement;
    expected_placement.references.clear();
    expected_placement.reference_valid = false;
    const auto verify = [&](const document::PartDocument& value) {
        const auto* feature = value.find_container(dependent.id);
        const auto retained = std::ranges::find(value.sketches, sketch.id, &sketcher::Sketch::id);
        require(feature && value.removed_reference_states.contains(feature->id) &&
                feature->placement == expected_placement,
            "A dependent feature did not retain its last valid placement");
        require(retained != value.sketches.end() && value.removed_reference_states.contains(sketch.id) &&
                frame(*retained) == saved_frame,
            "A Sketch moved off its last valid Plane after reference deletion");
    };
    workspace::Workspace live;
    const auto id = part.document_id;
    live.add_part(part, std::move(calculated));
    auto& session = live.open_part(id)->session;
    workspace::delete_part_history(live, id, kernel, plane.id);
    verify(session.document());
    require(std::abs(volume(session.calculated_boundaries()) - saved_volume) < 1e-8,
        "Reference deletion changed a dependent feature's retained solid");
    const auto path = std::filesystem::path("build") /
        ("history-retained-frames-" + sketch.id + ".prtz");
    session.document().save(path, session.calculated_boundaries());
    auto reopened = document::PartDocument::load(path);
    verify(reopened);
    const auto cold = workspace::calculate_part_with_resolved_references(kernel, reopened);
    verify(reopened);
    require(std::abs(volume(cold) - saved_volume) < 1e-8,
        "Cold reopening lost a dependent feature's retained solid");
    std::filesystem::remove(path);
    require(workspace::step_part_document_history(live, id, false) &&
            session.document().find_construction(plane.id),
        "Retained-frame deletion Undo did not restore the reference Plane");
    require(workspace::step_part_document_history(live, id, true), "Retained-frame deletion Redo failed");
    verify(session.document());
    require(std::abs(volume(session.calculated_boundaries()) - saved_volume) < 1e-8,
        "Reference deletion Redo lost a dependent feature's retained solid");
}
static void verify_retained_container_properties_repair(
        const kernel::OcctKernel& kernel) {
    auto part=document::PartDocument::create_default();
    auto first=test::rectangular_feature(part,{20,20,10});
    auto replacement=test::rectangular_feature(part,{12,12,8});
    replacement.placement.x=50;
    auto second=test::rectangular_feature(part,{8,8,6});
    auto third=test::rectangular_feature(part,{4,4,4});
    part.history={first,replacement,second,third};
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Three retained containers"));
    for(const auto& feature:part.history)
        graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(graph);
    auto calculated=workspace::calculate_part_with_resolved_references(
        kernel,part);
    const auto planar_face=[&](const std::string& owner) {
        const auto& references=calculated.back().mesh.original_references;
        const kernel::FaceReference* found{};
        for(const auto& reference:references.triangle_references)
            if(reference.owner_id==owner&&reference.surface&&
                    reference.surface->kind==
                        kernel::SurfaceGeometry::Kind::Plane&&
                    std::abs(reference.surface->axis.x)>.9&&
                    (!found||reference.surface->origin.x>
                        found->surface->origin.x))
                found=&reference;
        require(found,
            "Retained-container fixture has no planar face");
        document::ConstructionReference support;
        support.owner_id=found->owner_id;
        support.semantic_key=found->semantic_key;
        support.orientation_role="front";
        support.orientation_drives_rotation=true;
        return support;
    };
    const auto replacement_support=planar_face(replacement.id);
    part.find_container(second.id)->placement.references={planar_face(first.id)};
    part.resolve_constructions(calculated.back().mesh.original_references);
    calculated=workspace::calculate_part_with_resolved_references(
        kernel,part,&calculated);
    const auto third_support=planar_face(second.id);
    const auto third_support_face=std::ranges::find_if(
        calculated.back().mesh.original_references.triangle_references,
        [&](const auto& reference) {
            return reference.owner_id==third_support.owner_id&&
                reference.semantic_key==third_support.semantic_key;
        });
    require(third_support_face!=calculated.back().mesh.original_references.
            triangle_references.end()&&third_support_face->surface,
        "Retained-container fixture lost its follow surface");
    const auto follow_axis=third_support_face->surface->axis;
    part.find_container(third.id)->placement.references={third_support};
    part.resolve_constructions(calculated.back().mesh.original_references);
    calculated=workspace::calculate_part_with_resolved_references(
        kernel,part,&calculated);
    const auto third_before=*part.find_container(third.id);
    workspace::Workspace live;const auto document_id=part.document_id;
    live.add_part(part,calculated);
    workspace::delete_part_history(live,document_id,kernel,first.id);
    auto& session=live.open_part(document_id)->session;
    const auto& deleted=session.document();
    require(!deleted.find_container(first.id)&&
            deleted.find_container(second.id)&&
            deleted.find_container(third.id),
        "Deleting Container 1 removed Container 2 or Container 3");
    require(deleted.removed_reference_states.contains(second.id)&&
            !deleted.removed_reference_states.contains(third.id)&&
            deleted.find_container(second.id)->placement.references.empty()&&
            !deleted.find_container(second.id)->placement.reference_valid&&
            deleted.find_container(third.id)->placement.references.size()==1&&
            deleted.find_container(third.id)->placement.references.front().owner_id==second.id,
        "Reference deletion crossed the retained Container 2 boundary");
    const auto revision_after_delete=session.revision();
    auto edited=*deleted.find_container(second.id);
    edited.placement.reference_valid=true;
    edited.placement.x+=17*follow_axis.x;
    edited.placement.y+=17*follow_axis.y;
    edited.placement.z+=17*follow_axis.z;
    edited.extrusion.length_forward+=2;
    workspace::commit_profile(live,kernel,document_id,edited,
        workspace::ProfileEditMode::Replace);
    const auto& repaired=session.document();
    require(session.revision()==revision_after_delete+1&&
            !repaired.removed_reference_states.contains(second.id)&&
            !repaired.removed_reference_states.contains(third.id)&&
            repaired.find_container(second.id)->placement.reference_valid&&
            repaired.find_container(second.id)->placement.x==edited.placement.x&&
            repaired.find_container(second.id)->extrusion.length_forward==
                edited.extrusion.length_forward,
        "Container 2 Properties did not repair placement and dimensions");
    require(repaired.find_container(third.id)->placement.reference_valid&&
            repaired.find_container(third.id)->placement.references.size()==1&&
            repaired.find_container(third.id)->placement.references.front().owner_id==second.id&&
            repaired.find_container(third.id)->placement!=third_before.placement,
        "Container 3 did not remain attached to the repaired Container 2");
    require(session.calculated_boundaries().back().calculation_errors.empty(),
        "Retained-container repair left a calculation error");
    const auto repaired_serialized=repaired.serialized();
    require(workspace::step_part_document_history(live,document_id,false)&&
            session.document().removed_reference_states.contains(second.id),
        "Retained-container repair Undo lost the deleted-reference state");
    require(workspace::step_part_document_history(live,document_id,true)&&
            session.document().serialized()==repaired_serialized,
        "Retained-container repair Redo changed the repaired chain");
    const auto path=std::filesystem::absolute(
        "build/history-deletion-three-containers.prtz");
    session.document().save(path,session.calculated_boundaries());
    std::vector<kernel::BodyResult> reopened_calculated;
    const auto reopened=document::PartDocument::load(path,&reopened_calculated);
    require(!reopened.removed_reference_states.contains(second.id)&&
            reopened.find_container(second.id)->placement.x==edited.placement.x&&
            reopened.find_container(third.id)->placement.reference_valid&&
            !reopened_calculated.empty()&&
            reopened_calculated.back().calculation_errors.empty(),
        "Saved retained-container repair did not reopen intact");
    std::filesystem::remove(path);
    require(workspace::step_part_document_history(live,document_id,false)&&
            session.document().removed_reference_states.contains(second.id),
        "Placement-rebind Undo did not restore the repairable state");
    auto rebound=*session.document().find_container(second.id);
    rebound.placement.references={replacement_support};
    rebound.placement.reference_valid=true;
    workspace::commit_profile(live,kernel,document_id,rebound,
        workspace::ProfileEditMode::Replace);
    require(!session.document().removed_reference_states.contains(second.id),
        "Rebound Container 2 retained its deleted-reference state");
    require(session.document().find_container(second.id)->placement.reference_valid,
        "Rebound Container 2 placement is invalid");
    require(session.document().find_container(second.id)->placement.references==
            rebound.placement.references,
        "Container 2 replacement placement reference was not persisted");
    require(session.document().find_container(third.id)->placement.reference_valid,
        "Container 3 placement became invalid after Container 2 rebinding");
    require(session.document().find_container(third.id)->placement.references.front().owner_id==second.id,
        "Container 3 stopped referencing Container 2 after rebinding");
    require(session.calculated_boundaries().back().calculation_errors.empty(),
        "Container 2 rebinding left a calculation error");
}
int main() {
    try {
        kernel::OcctKernel kernel;
        verify_live_regeneration(kernel);verify_cold_history_resume();verify_recovery_prefix();
        verify_sheet_suppression(kernel);
        verify_retained_feature_and_sketch_frames(kernel);
        verify_retained_container_properties_repair(kernel);
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
        {
            auto bodies_document=document::PartDocument::create_default();
            auto first_feature=test::rectangular_feature(bodies_document,{20,20,10});
            auto sketch_container=document::PartDocument::create_sketch_container();
            auto editable_sketch=sketcher::Sketch::create_default();
            editable_sketch.owner_container_id=sketch_container.id;
            sketch_container.feature.sketch_id=editable_sketch.id;
            auto second_feature=test::rectangular_feature(bodies_document,{5,5,5});
            bodies_document.history={first_feature,sketch_container,second_feature};
            bodies_document.sketches.push_back(editable_sketch);
            document::BodyHistoryGraph bodies_graph;
            const auto first_body=bodies_graph.create_body("First");
            bodies_graph.insert({document::PartHistoryKind::Feature,first_feature.id});
            const auto second_body=bodies_graph.create_body("Second");
            bodies_graph.insert({document::PartHistoryKind::Feature,sketch_container.id});
            bodies_graph.insert({document::PartHistoryKind::Feature,second_feature.id});
            bodies_document.set_body_history(bodies_graph);
            auto bodies_calculated=workspace::calculate_part_with_resolved_references(kernel,bodies_document);
            const auto face=std::ranges::find_if(bodies_calculated.back().mesh.original_references.triangle_references,
                [&](const auto& value){return value.owner_id==first_feature.id&&value.surface&&
                    value.surface->kind==kernel::SurfaceGeometry::Kind::Plane;});
            require(face!=bodies_calculated.back().mesh.original_references.triangle_references.end(),
                "Two-Body deletion fixture has no source face");
            bodies_graph=bodies_document.body_history;
            auto positioned=*bodies_graph.find(second_body);
            document::ConstructionReference placement;
            placement.owner_id=face->owner_id;placement.semantic_key=face->semantic_key;
            placement.orientation_role="front";placement.orientation_drives_rotation=true;
            positioned.scope.placement.references={placement};
            bodies_graph.update_body(positioned);bodies_document.set_body_history(bodies_graph);
            bodies_document.resolve_constructions(bodies_calculated.back().mesh.original_references);
            bodies_calculated=workspace::calculate_part_with_resolved_references(kernel,bodies_document,&bodies_calculated);
            require(std::ranges::find(bodies_document.body_history.find(second_body)->dependencies,first_body)!=
                    bodies_document.body_history.find(second_body)->dependencies.end(),
                "Second Body did not persist its placement dependency");
            auto retained_body_placement = bodies_document.body_history.find(second_body)->scope.placement;
            retained_body_placement.references.clear();
            retained_body_placement.reference_valid = false;
            workspace::Workspace bodies_live;const auto bodies_id=bodies_document.document_id;
            bodies_live.add_part(bodies_document,bodies_calculated);
            workspace::delete_part_history(bodies_live,bodies_id,kernel,first_body);
            auto& bodies_session=bodies_live.open_part(bodies_id)->session;
            const auto& after=bodies_session.document();
            const auto* surviving_body=after.body_history.find(second_body);
            require(!after.body_history.find(first_body)&&surviving_body&&after.find_container(sketch_container.id)&&
                std::ranges::any_of(after.sketches,[&](const auto& value){return value.id==editable_sketch.id;}),
                "Deleting the first Body removed the dependent Body or its Sketch");
            require(surviving_body->scope.placement.references.empty()&&surviving_body->dependencies.empty()&&
                after.removed_reference_states.contains(second_body),
                "Deleting the first Body retained an unusable Body placement reference");
            require(surviving_body->scope.placement == retained_body_placement,
                "Dependent Body did not retain its last valid frame");
            require(bodies_session.rollback_boundary(sketch_container.id).has_value()&&
                !bodies_session.calculated_boundaries().empty(),
                "Dependent Body Sketch cannot open after its source Body is deleted");
            const auto reopen_path=std::filesystem::absolute(
                "build/history-deletion-two-bodies.prtz");
            after.save(reopen_path,bodies_session.calculated_boundaries());
            std::vector<kernel::BodyResult> reopened_calculated;
            const auto reopened=document::PartDocument::load(reopen_path,&reopened_calculated);
            document::DocumentSession reopened_session(reopened,reopened_calculated);
            require(reopened.body_history.find(second_body)&&
                reopened.body_history.find(second_body)->scope.placement == retained_body_placement &&
                reopened.removed_reference_states.contains(second_body)&&
                reopened_session.rollback_boundary(sketch_container.id).has_value(),
                "Saved two-Body deletion did not reopen with an editable surviving Sketch");
            std::filesystem::remove(reopen_path);
            require(workspace::step_part_document_history(bodies_live,bodies_id,false)&&
                bodies_session.document().body_history.find(first_body)&&
                !bodies_session.document().body_history.find(second_body)->scope.placement.references.empty(),
                "Two-Body deletion Undo did not restore the placement reference");
            require(workspace::step_part_document_history(bodies_live,bodies_id,true)&&
                !bodies_session.document().body_history.find(first_body)&&
                bodies_session.rollback_boundary(sketch_container.id).has_value(),
                "Two-Body deletion Redo blocked the surviving Sketch");
        }
        auto pattern_doc=document::PartDocument::create_default();
        auto pattern_source=test::rectangular_feature(pattern_doc,{4,4,4});
        auto pattern=document::PartDocument::create_fillet_container({reference});
        pattern.feature_kind=document::FeatureKind::DerivedCopy;pattern.name="Pattern";
        pattern.derived_copy.source_ids={pattern_source.id};pattern.derived_copy.pattern=kernel::PatternRequest{};
        pattern_doc.history={pattern_source,pattern};
        document::BodyHistoryGraph pattern_graph;static_cast<void>(pattern_graph.create_body("Pattern"));
        for(const auto& f:pattern_doc.history)pattern_graph.insert({document::PartHistoryKind::Feature,f.id});
        pattern_doc.set_body_history(pattern_graph);
        workspace::Workspace pattern_live;const auto pattern_id=pattern_doc.document_id;
        pattern_live.add_part(pattern_doc,workspace::calculate_part(kernel,pattern_doc));
        workspace::delete_part_history(pattern_live,pattern_id,kernel,pattern_source.id);
        const auto pattern_roundtrip=document::PartDocument::from_serialized(pattern_live.open_part(pattern_id)->session.document().serialized());
        require(pattern_roundtrip.find_container(pattern.id)&&pattern_roundtrip.find_container(pattern.id)->derived_copy.source_ids.empty()&&
            pattern_roundtrip.removed_reference_states.contains(pattern.id),"Pattern without source could not remain in native history");
        std::cout<<"History deletion contracts passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
