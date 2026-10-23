#include <zima/document/general_surface.hpp>
#include "../modules/kernel_occt/src/ordered_up_to.hpp"
#include <zima/workspace/general_surface_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <QCoreApplication>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <sstream>

using namespace zima;
namespace {
void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-5)
    throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
template<class Doc> unsigned correction_relations() {
    unsigned cases=0;
    for(bool angular:{false,true})for(bool reversed:{false,true})for(std::size_t side:{0u,1u}) {
        auto doc=Doc::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.reversed_sides=reversed;
        for(auto& ending:feature.feature.sides) {
            ending.operation=angular?document::FeatureSideOperation::Revolution:document::FeatureSideOperation::Extrusion;
            ending.extrusion_extent=document::EndCondition::UpTo;ending.rotation_extent=document::FeatureRotationExtent::UpTo;
            document::ExtrusionParameters::EndTarget target;target.reference={"relation-source","plane",{}};ending.targets={target};
            ending.up_to.angle_correction_degrees=9;ending.up_to.length_correction=11;
        }
        if constexpr(requires {doc.history;})doc.history={feature};
        else doc.cuts.push_back({feature,{}});
        doc.sketches={sketch};doc.document_units["Length"]="cm";doc.document_units["Angle"]="rad";
        doc.synchronize_dimension_identifiers();
        const auto key="side"+std::to_string(side)+(angular?"_up_to_angle_correction":"_up_to_length_correction");
        const auto symbol=doc.dimension_identifiers.identifier(feature.id,"parameter:"+key);
        check(!symbol.empty(),"Correction has no stable relation dimension identifier");
        const auto access=[&]() -> document::HistoryContainer& {
            if constexpr(requires {doc.history;})return doc.history.front();else return doc.cuts.front().definition;
        };
        const double scale=angular?180./std::numbers::pi:10.;
        doc.relations=symbol+" = amount\n";
        for(double value:{2.,-1.5,0.,.75}) {
            doc.user_parameters["amount"]=std::to_string(value);
            workspace::apply_relation_dimensions(doc,{});
            const auto& current=access().feature;
            near(angular?current.sides[side].up_to.angle_correction_degrees:current.sides[side].up_to.length_correction,value*scale);
            near(angular?current.sides[side].up_to.length_correction:current.sides[side].up_to.angle_correction_degrees,angular?11.:9.);
            check(current.reversed_sides==reversed,"Signed correction changed the Feature direction");
            const auto input=workspace::relation_dimensions(doc).at(symbol);
            check(input.input.value.units==(angular?std::array{0,1,0}:std::array{1,0,0})&&input.input.writable,
                "Correction relation has the wrong quantity or is not writable");
        }
        access().value_locks.insert(key);
        check(!workspace::relation_dimensions(doc).at(symbol).input.writable,"Correction lock was ignored by Relations");
        const auto before=access().feature;bool rejected=false;
        try{static_cast<void>(workspace::assign_driving_dimension(doc,{"dimension",feature.id,"parameter:"+key},-2));}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&access().feature==before,"Locked correction edit changed the native definition");++cases;
    }
    return cases;
}
double volume(const kernel::OcctKernel& kernel,const workspace::PartState& state) {
    auto result=state.session.calculated_boundaries().back();kernel.ensure_body_properties(result);
    for(const auto& [owner,message]:result.calculation_errors)std::cerr<<owner<<": "<<message<<'\n';
    if(!result.calculation_errors.empty())try{static_cast<void>(kernel.evaluate_history(state.session.document().kernel_operations()));}
        catch(const kernel::detail::OrderedLimitError& error){std::cerr<<"Ordered reason="<<static_cast<int>(error.failure)<<" rows="<<error.row<<","<<error.other_row<<'\n';throw;}
    check(result.calculation_errors.empty(),"Native ordered Feature has calculation errors");return result.volume;
}
document::HistoryContainer source(double a,double b,double first,double last,unsigned row) {
    auto feature=document::create_general_surface();feature.name="Limit target "+std::to_string(row);
    const std::array<kernel::Vec3,4> corners{{{first,-2,a*first+b},{last,-2,a*last+b},{last,5,a*last+b},{first,5,a*first+b}}};
    for(unsigned i=0;i<4;++i) {
        feature.general_surface.boundaries[i]=document::create_general_surface_curve(feature);
        auto& curve=*feature.general_surface.boundaries[i].curve;curve.curve_type=document::Curve3DType::Polyline;curve.curve_points.clear();
        for(const auto point:{corners[i],corners[(i+1)%4]}) {auto owned=document::create_owned_point(curve.id);owned.origin=point;curve.curve_points.push_back(owned);}
    }
    return feature;
}
unsigned independent_limits(const std::filesystem::path& output) {
    kernel::OcctKernel engine;workspace::Workspace live;auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Independent sides"));part.set_body_history(graph);
    const auto id=part.document_id;live.add_part(part);live.activate(id);
    const std::array<double,3> a{{1,0,-.5}},b{{10,12,15}},first{{-2,2.3,6.3}},last{{1.7,5.7,10}};
    std::array<std::vector<document::HistoryContainer>,2> sources;
    for(std::size_t side=0;side<2;++side)for(unsigned row=0;row<3;++row) {
        const double sign=side?-1.:1.;auto target=source(sign*a[row],sign*b[row],first[row],last[row],row);
        check(workspace::commit_general_surface(live,engine,id,target),"Independent source did not commit");sources[side].push_back(target);
    }
    auto* state=live.open_part(id);auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,0,10,3));
    auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
    for(std::size_t side=0;side<2;++side) {
        auto& end=feature.feature.sides[side];end.operation=document::FeatureSideOperation::Extrusion;end.extrusion_extent=document::EndCondition::UpTo;
        for(const auto& target:sources[side]) {
            document::ExtrusionParameters::EndTarget requested;requested.reference={target.id,"surface:from:"+target.feature_id,{}};
            end.targets.push_back(workspace::resolve_profile_end_target(state->session.document(),state->session.calculated_boundaries(),graph.active_body_id(),requested));
        }
    }
    workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,sketch);near(volume(engine,*state),792.);
    auto pending=*state->session.document().find_container(feature.id);
    check(pending.feature.sides[0].up_to.primary_cap_source!=pending.feature.sides[1].up_to.primary_cap_source&&
        pending.feature.sides[0].up_to.retained_patches.size()==4&&pending.feature.sides[1].up_to.retained_patches.size()==4,
        "Independent zero-correction sides merged their native choices");
    pending.feature.sides[0].up_to.length_correction=1.25;pending.feature.sides[1].up_to.length_correction=-.75;
    workspace::commit_profile(live,engine,id,pending,workspace::ProfileEditMode::Replace);near(volume(engine,*state),810.);
    auto before=state->session.calculated_boundaries().back();engine.ensure_body_properties(before);
    const auto unchanged=state->session.document().serialized();const auto revision=state->session.revision();
    pending=*state->session.document().find_container(feature.id);pending.feature.reversed_sides=true;bool rejected=false;
    try{workspace::commit_profile(live,engine,id,pending,workspace::ProfileEditMode::Replace);}catch(const std::exception&){rejected=true;}
    check(rejected&&state->session.revision()==revision&&state->session.document().serialized()==unchanged,
        "Reversing toward fixed targets on the wrong side changed the native document");
    std::swap(pending.feature.sides[0].targets,pending.feature.sides[1].targets);
    for(auto& side:pending.feature.sides)document::prune_feature_up_to_patch_choices(side);
    workspace::commit_profile(live,engine,id,pending,workspace::ProfileEditMode::Replace);near(volume(engine,*state),810.);
    auto after=state->session.calculated_boundaries().back();engine.ensure_body_properties(after);
    check(before.volume_integrals&&after.volume_integrals,"Independent side analysis has no exact centroid");
    near(after.volume_integrals->centroid.z,-before.volume_integrals->centroid.z);
    check(state->session.document().find_container(feature.id)->feature.sides[0].up_to.length_correction==1.25&&
        state->session.document().find_container(feature.id)->feature.sides[1].up_to.length_correction==-.75,
        "Reversal exchanged the two logical panels' corrections");
    const auto file=output/"independent-sides.prtz";state->session.document().save(file,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cached;auto reopened=document::PartDocument::load(file,&cached);
    workspace::Workspace cold_live;kernel::OcctKernel cold;cold_live.add_part(reopened,cached,file);
    static_cast<void>(workspace::regenerate_part(cold_live,cold,id));near(volume(cold,*cold_live.open_part(id)),810.);
    return 5;
}
unsigned assembly_limit_sources(const std::filesystem::path& source_file,const std::string& source_feature,
        const std::vector<kernel::FaceReference>& target_parents) {
    kernel::OcctKernel engine;workspace::Workspace live;
    std::vector<kernel::BodyResult> cached;auto limits=document::PartDocument::load(source_file,&cached);
    const auto native_source=cached.back();live.add_part(limits,cached,source_file);
    auto stock=document::PartDocument::create_default();auto profile=sketcher::Sketch::create_default();
    static_cast<void>(profile.add_rectangle(-2,0,10,3));auto box=document::PartDocument::create_feature_container(profile.id);
    profile.owner_container_id=box.id;box.feature.sides[0].length=30;stock.sketches={profile};stock.history={box};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Stock"));
    graph.insert({document::PartHistoryKind::Feature,box.id});stock.set_body_history(graph);stock.resolve_constructions();
    auto stock_geometry=engine.evaluate_history(stock.kernel_operations());engine.ensure_body_properties(stock_geometry.back());
    const auto stock_file=source_file.parent_path()/"assembly-stock.prtz";stock.save(stock_file,stock_geometry);
    live.add_part(stock,stock_geometry,stock_file);
    auto assembly=assembly::AssemblyDocument::create_default();
    auto input=assembly::AssemblyDocument::create_part_occurrence("Limits",limits.document_id,source_file,native_source);
    auto block=assembly::AssemblyDocument::create_part_occurrence("Stock",stock.document_id,stock_file,stock_geometry.back());
    const auto input_id=input.occurrence_id,block_id=block.occurrence_id;
    assembly.components={input,block};assembly.synchronize_history();const auto id=assembly.document_id;
    live.add_assembly(assembly);live.activate(id);
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,0,10,3));
    auto cut=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=cut.id;
    cut.combine_mode=document::CombineMode::Subtract;cut.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
    // Source rows are defined by their original cap ancestry, not by face order
    // or a geometric nearest-face lookup in the test.
    for(std::size_t row=0;row<target_parents.size();++row) {
        std::string suffix=":limit";
        for(const auto& text:{target_parents[row].owner_id,target_parents[row].semantic_key,target_parents[row].instance_path})
            suffix+=":"+std::to_string(text.size())+":"+text;
        std::optional<kernel::FaceReference> selected;
        for(const auto& ref:native_source.mesh.original_references.triangle_references)
            if(ref.owner_id==source_feature&&ref.semantic_key.starts_with("end:from:")&&
                    (row==0?ref.semantic_key.find(":limit:")==std::string::npos:ref.semantic_key.ends_with(suffix))) {
                const auto separator=ref.semantic_key.find(':',9);
                const auto count=std::stoul(ref.semantic_key.substr(9,separator-9));
                const auto parent=kernel::feature_side_parent(ref.semantic_key.substr(separator+1,count));
                if(!parent||parent->side!=kernel::FeatureSide::End)continue;
                kernel::FaceReference candidate{ref.owner_id,ref.semantic_key,assembly::InstancePath{}.child(input_id).encoded()};
                if(selected&&*selected!=candidate)throw std::runtime_error("Source cap ancestry is ambiguous: "+
                    selected->semantic_key+" <> "+candidate.semantic_key);
                selected=candidate;
            }
        check(selected.has_value(),"Source cap ancestry is missing");
        document::ExtrusionParameters::EndTarget target;target.reference=*selected;cut.feature.sides[0].targets.push_back(target);
    }
    workspace::commit_assembly_profile(live,engine,id,cut,{block_id},workspace::ProfileEditMode::Create,sketch);
    auto* state=live.open_assembly(id);near(state->session.document().find_occurrence(block_id)->calculated_source->volume,657.);
    check(state->session.document().find_occurrence(input_id)->calculated_source->source_fingerprint==native_source.source_fingerprint,
        "Assembly cutter changed the source Part snapshot");
    const auto accepted=state->session.document().serialized();
    check(state->session.document().find_cut(cut.id)->definition.feature.sides[0].up_to.retained_patches.size()==4,
        "Assembly did not persist accepted native retained choices");
    check(state->session.undo()&&state->session.document().cuts.empty(),"Assembly creation Undo retained the cut");
    check(state->session.redo()&&state->session.document().serialized()==accepted,"Assembly creation Redo lost native limit intent");
    auto changed=state->session.document().find_cut(cut.id)->definition;changed.feature.sides[0].up_to.length_correction=-1.5;
    workspace::commit_assembly_profile(live,engine,id,changed,{block_id},workspace::ProfileEditMode::Replace);
    near(state->session.document().find_occurrence(block_id)->calculated_source->volume,711.);
    const auto revision=state->session.revision();
    workspace::commit_assembly_profile(live,engine,id,state->session.document().find_cut(cut.id)->definition,{block_id},workspace::ProfileEditMode::Replace);
    check(state->session.revision()==revision,"Unchanged Assembly ordered limit created a transaction");
    const auto file=source_file.parent_path()/"assembly-ordered-limits.asmz";state->session.document().save(file);
    auto reopened=assembly::AssemblyDocument::load(file);
    check(reopened.find_cut(cut.id)->definition.feature==state->session.document().find_cut(cut.id)->definition.feature,
        "Assembly save/reopen changed ordered limit intent");
    workspace::Workspace cold_live;kernel::OcctKernel cold;cold_live.add_assembly(std::move(reopened),file);cold_live.activate(id);
    workspace::regenerate_assembly(cold_live,cold,id);
    near(cold_live.open_assembly(id)->session.document().find_occurrence(block_id)->calculated_source->volume,711.);
    auto moved=cold_live.open_assembly(id)->session.document();moved.find_occurrence(input_id)->placement.z=.5;
    cold_live.open_assembly(id)->session.commit(std::move(moved));workspace::regenerate_assembly(cold_live,cold,id);
    near(cold_live.open_assembly(id)->session.document().find_occurrence(block_id)->calculated_source->volume,693.);
    check(cold_live.open_assembly(id)->session.document().find_occurrence(input_id)->calculated_source->source_fingerprint==native_source.source_fingerprint,
        "Moving the source occurrence rewrote its source Part geometry");
    return 6;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try {
    unsigned cases=correction_relations<document::PartDocument>()+correction_relations<assembly::AssemblyDocument>();
    const auto output=std::filesystem::temp_directory_path()/
        ("zima-ordered-feature-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(output),"Native evidence directory already exists");
    cases+=independent_limits(output);
    for(bool concave:{false,true})for(bool symmetric:{false,true}) {
        kernel::OcctKernel kernel;workspace::Workspace live;auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Ordered limits"));part.set_body_history(graph);
        const auto id=part.document_id;live.add_part(part);live.activate(id);
        const std::array<std::array<double,2>,3> planes=concave?
            std::array<std::array<double,2>,3>{{{{-1,12}},{{0,10}},{{1,4}}}}:
            std::array<std::array<double,2>,3>{{{{1,10}},{{0,12}},{{-.5,15}}}};
        const std::array<double,3> first{{-2,2.3,6.3}},last{{1.7,5.7,10}};
        std::vector<document::HistoryContainer> sources;
        for(unsigned row=0;row<3;++row) {
            auto target=source(planes[row][0],planes[row][1],first[row],last[row],row);
            check(workspace::commit_general_surface(live,kernel,id,target),"Native source creation did not commit");sources.push_back(target);
        }
        auto* state=live.open_part(id);const auto source_history=state->session.document().history;
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,0,10,3));
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.symmetric=symmetric;feature.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
        for(const auto& target:sources) {
            document::ExtrusionParameters::EndTarget requested;requested.reference={target.id,"surface:from:"+target.feature_id,{}};
            feature.feature.sides[0].targets.push_back(workspace::resolve_profile_end_target(state->session.document(),
                state->session.calculated_boundaries(),graph.active_body_id(),requested));
        }
        try{workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Create,sketch);}
        catch(const std::exception&) {
            for(const auto& target:sources) {
                const auto body=kernel.evaluate_history({{target.id,document::general_surface_request(state->session.document(),target)}}).back();
                BRep_Builder builder;TopoDS_Shape shape;std::istringstream input(body.kernel_shape);BRepTools::Read(shape,input,builder);
                for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
                    const auto basis=BRep_Tool::Surface(TopoDS::Face(face.Current()));
                    GeomLib_IsPlanarSurface planar(basis,1e-7);double deviation=0;
                    if(planar.IsPlanar())if(auto spline=occ::down_cast<Geom_BSplineSurface>(basis))
                        for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v)
                            deviation=std::max(deviation,planar.Plan().Distance(spline->Pole(u,v)));
                    std::cerr<<target.name<<" plane="<<planar.IsPlanar()<<" pole_deviation="<<deviation<<'\n';
                }
            }
            auto candidate=state->session.document();candidate.sketches.push_back(sketch);
            candidate.insert_history_entry(document::PartHistoryKind::Feature,feature.id);candidate.history.push_back(feature);candidate.resolve_constructions();
            try{static_cast<void>(kernel.evaluate_history(candidate.kernel_operations()));}
            catch(const kernel::detail::OrderedLimitError& error){std::cerr<<"Create reason="<<static_cast<int>(error.failure)<<" rows="<<error.row<<","<<error.other_row<<'\n';}
            throw;
        }
        state=live.open_part(id);
        const double base=concave?408.:396.,factor=symmetric?2.:1.;near(volume(kernel,*state),factor*base);
        const auto& stored=state->session.document().find_container(feature.id)->feature;
        check(stored.sides[0].up_to.retained_patches.size()==4,"Accepted retained sides were not stored in the native owner");
        check(stored.sides[0].up_to.primary_cap_source==feature.feature.sides[0].targets.front().reference,
            "The original cap was not anchored to its authored target");
        for(const auto& old:source_history)check(*state->session.document().find_container(old.id)==old,"Feature creation modified a reference source");++cases;
        const auto revision=state->session.revision();const auto* calculated=state->session.calculated_boundaries().data();
        workspace::commit_profile(live,kernel,id,*state->session.document().find_container(feature.id),workspace::ProfileEditMode::Replace);
        check(state->session.revision()==revision&&state->session.calculated_boundaries().data()==calculated,
            "Unchanged ordered Feature OK recalculated or created an Undo step");++cases;
        check(state->session.undo()&&!state->session.document().find_container(feature.id),"Ordered Feature creation Undo failed");
        check(state->session.redo()&&state->session.document().find_container(feature.id),"Ordered Feature creation Redo failed");near(volume(kernel,*state),factor*base);++cases;
        for(double correction:{1.5,-1.5,0.,.75}) {
            const auto before=state->session.document().serialized();auto changed=*state->session.document().find_container(feature.id);
            const auto choices=changed.feature.sides[0].up_to.retained_patches;
            changed.feature.sides[0].up_to.length_correction=correction;
            workspace::commit_profile(live,kernel,id,changed,workspace::ProfileEditMode::Replace);near(volume(kernel,*state),factor*(base+36*correction));
            check(state->session.document().find_container(feature.id)->feature.sides[0].up_to.retained_patches==choices,
                "A correction edit reselected a retained source side");
            const auto after=state->session.document().serialized();check(state->session.undo()&&state->session.document().serialized()==before,"Correction Undo changed the prior document");
            check(state->session.redo()&&state->session.document().serialized()==after,"Correction Redo did not restore the accepted definition");++cases;
        }
        auto reordered=*state->session.document().find_container(feature.id);
        std::ranges::reverse(reordered.feature.sides[0].targets);document::prune_feature_up_to_patch_choices(reordered.feature.sides[0]);
        workspace::commit_profile(live,kernel,id,reordered,workspace::ProfileEditMode::Replace);near(volume(kernel,*state),factor*(base+27));++cases;
        check(state->session.document().find_container(feature.id)->feature.sides[0].up_to.primary_cap_source==
            feature.feature.sides[0].targets.front().reference,"Row reordering transferred the original cap to another plane");
        const auto& packet=state->session.calculated_boundaries().back().mesh.original_references;
        std::size_t original_cap_triangles=0;
        for(std::size_t i=0;i<packet.triangle_references.size();++i) {
            const auto& ref=packet.triangle_references[i];
            if(ref.owner_id!=feature.id||ref.semantic_key.find(":limit:")!=std::string::npos||
                (!ref.semantic_key.starts_with("end:from:")&&!ref.semantic_key.starts_with("start:from:")))continue;
            for(unsigned j=0;j<3;++j) {
                const auto point=packet.vertices[packet.triangles[3*i+j]];
                if(std::abs(point.z)<1e-7)continue;
                near(std::abs(point.z),planes[0][0]*point.x+planes[0][1]+.75);++original_cap_triangles;
            }
        }
        check(original_cap_triangles>0,"Native reordered Feature lost its original cap reference");
        const auto before_bad=state->session.document().serialized();const auto bad_revision=state->session.revision();
        auto invalid=*state->session.document().find_container(feature.id);std::swap(invalid.feature.sides[0].targets[0],invalid.feature.sides[0].targets[1]);
        document::prune_feature_up_to_patch_choices(invalid.feature.sides[0]);bool rejected=false;
        try{workspace::commit_profile(live,kernel,id,invalid,workspace::ProfileEditMode::Replace);}
        catch(const kernel::OrderedUpToError& error){rejected=error.row<3&&error.other_row<3;}
        check(rejected&&state->session.document().serialized()==before_bad&&state->session.revision()==bad_revision,
            "An invalid target order changed the native session");++cases;
        auto missing=*state->session.document().find_container(feature.id);
        missing.feature.sides[0].targets[1].reference.owner_id="missing-ordered-source";
        document::prune_feature_up_to_patch_choices(missing.feature.sides[0]);rejected=false;
        try{workspace::commit_profile(live,kernel,id,missing,workspace::ProfileEditMode::Replace);}
        catch(const kernel::OrderedUpToError& error){rejected=error.failure==kernel::OrderedUpToFailure::InvalidInput&&error.row==1&&error.other_row==1;}
        check(rejected&&state->session.document().serialized()==before_bad&&state->session.revision()==bad_revision,
            "Missing source rejection lost its row or changed the native session");++cases;
        const auto file=output/(std::string(concave?"concave":"convex")+(symmetric?"-symmetric":"-end")+".prtz");
        state->session.document().save(file,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cache;auto reopened=document::PartDocument::load(file,&cache);
        check(reopened.find_container(feature.id)->feature==state->session.document().find_container(feature.id)->feature,
            "Native save/reopen changed the ordered limit definition");
        for(const auto& boundary:cache)check(boundary.resolved_up_to_limits.empty(),"Ephemeral resolution receipts were persisted instead of native intent");
        kernel::OcctKernel cold;workspace::Workspace loaded;loaded.add_part(reopened,cache,file);loaded.activate(id);
        static_cast<void>(workspace::regenerate_part(loaded,cold,id));near(volume(cold,*loaded.open_part(id)),factor*(base+27));++cases;
        if(!concave&&!symmetric) {
            std::vector<kernel::FaceReference> parents;
            for(const auto& target:feature.feature.sides[0].targets)parents.push_back(target.reference);
            cases+=assembly_limit_sources(file,feature.id,parents);
        }
    }
    std::cout<<"PASS "<<cases<<" native ordered Feature checks: original finite faces, automatic choice persistence, symmetry, repeated corrections, unchanged OK, row identity/rejection, Undo/Redo, native save/reopen and explicit cold regeneration. Evidence: "<<output.generic_string()<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
