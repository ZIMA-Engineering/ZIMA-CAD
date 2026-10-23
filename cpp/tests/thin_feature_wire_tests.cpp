#include "../app/authored_feature_wire.hpp"
#include "thin_wire_verification.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <QCoreApplication>
#include <iostream>
#include <set>
#include <cmath>

using namespace zima;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
bool longitudinal(const kernel::ViewerEdge& e,kernel::Vec3 normal) {
    if(e.points.size()!=2)return false;
    const auto a=e.points.front(),b=e.points.back();
    const kernel::Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const auto length=std::hypot(d.x,d.y,d.z);
    return length>1e-7&&std::abs(std::abs(d.x*normal.x+d.y*normal.y+d.z*normal.z)-length)<1e-7;
}
void rectangular_matrix(const kernel::OcctKernel& engine) {
    unsigned fillets{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(bool reversed:{false,true}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(0,0,20,12));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        sketch.owner_container_id=feature.id;feature.feature.result_type=document::ProfileResultType::Thin;
        feature.feature.thin_mode=mode;feature.feature.thin_thickness=1;feature.feature.sides[0].length=20;
        feature.feature.reversed_sides=reversed;
        doc.history={feature};doc.sketches={sketch};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Thin wire"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        doc.set_body_history(graph);doc.resolve_constructions();sketch=doc.sketches.front();
        const auto wire=doc.feature_preview_edges(feature,{});
        tests::verify_rectangular_thin_start(doc,feature,wire);
        std::vector<kernel::ViewerEdge> rails;for(const auto& edge:wire)if(longitudinal(edge,sketch.normal()))rails.push_back(edge);
        require(rails.size()==8,"A rectangular Thin preview must contain all eight longitudinal edges, without duplicates");
        const double outer=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?-1:-.5;
        const double inner=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
        std::set<std::array<long long,2>> expected,actual;
        for(double offset:{outer,inner})for(double x:{offset,20-offset})for(double y:{offset,12-offset})
            expected.insert({std::llround(x*1e7),std::llround(y*1e7)});
        for(const auto& edge:rails) {
            const auto p=sketch.local_point(edge.points.front());actual.insert({std::llround(p[0]*1e7),std::llround(p[1]*1e7)});
            const auto a=edge.points.front(),b=edge.points.back();
            require(std::abs(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)-20)<1e-7,"Thin rail does not end at its authored length");
        }
        require(actual==expected,"Thin rails do not match the independently calculated inner/outer rectangle corners");
        auto boundaries=engine.evaluate_history(doc.kernel_operations());
        require(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"Thin fillet input failed");
        const auto& local=boundaries.back().body_boundaries.at(graph.active_body_id()).back();
        std::vector<kernel::EdgeReference> edges;
        for(const auto& edge:local.mesh.edges)if(edge.reference.owner_id==feature.id&&longitudinal(edge,sketch.normal())&&
            edge.reference.semantic_key.starts_with("generated:thin:outside:from:"))edges.push_back(edge.reference);
        require(edges.size()==4,"Thin native input did not expose all four outside longitudinal edges");
        for(double radius:{.1,.25,.4}) {
            workspace::Workspace live;live.add_part(doc,boundaries);live.activate(doc.document_id);
            auto treatment=document::PartDocument::create_fillet_container({edges[0],edges[1]});
            treatment.edge_treatment.primary_size=treatment.edge_treatment.secondary_size=radius;
            treatment.edge_treatment.routes={{edges[0]},{edges[1]}};
            require(workspace::commit_edge_treatment(live,engine,doc.document_id,treatment,workspace::EdgeTreatmentEditMode::Create),
                "Two-edge Thin fillet did not commit");
            auto& state=*live.open_part(doc.document_id);
            require(state.session.calculated_boundaries().back().calculation_errors.empty(),"Two-edge Thin fillet retained a calculation failure");
            require(state.session.undo()&&state.session.redo(),"Two-edge Thin fillet Undo/Redo failed");++fillets;
        }
    }
    std::cout<<"Thin rectangle matrix: 12 plane/mode/direction variants, eight exact rails each, "<<fillets<<" two-edge fillets passed\n";
}
void revolution_matrix(const kernel::OcctKernel& engine) {
    unsigned cases{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(bool reversed:{false,true}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(5,0,25,12));const auto axis=sketch.add_segment(0,0,0,12,1e-6,true);
        sketch.segments.back().centerline=true;
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.axis_segment_id=axis;feature.feature.result_type=document::ProfileResultType::Thin;
        feature.feature.thin_mode=mode;feature.feature.thin_thickness=1;feature.feature.reversed_sides=reversed;
        feature.feature.sides[0].operation=document::FeatureSideOperation::Revolution;feature.feature.sides[0].angle_degrees=90;
        doc.history={feature};doc.sketches={sketch};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Thin revolution wire"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        doc.set_body_history(graph);doc.resolve_constructions();sketch=doc.sketches.front();
        const auto wire=doc.feature_preview_edges(feature,{});unsigned sweeps{};
        std::set<std::array<long long,2>> expected,actual;
        const double outside=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?1:.5;
        const double inside=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
        for(double d:{-outside,inside})for(double x:{5+d,25-d})for(double y:{d,12-d})expected.insert({std::llround(x*1e7),std::llround(y*1e7)});
        for(const auto& edge:wire)if(edge.reference.semantic_key.starts_with("preview:sweep")) {
            ++sweeps;const auto p=sketch.local_point(edge.points.front());actual.insert({std::llround(p[0]*1e7),std::llround(p[1]*1e7)});
            for(const auto point:edge.points) {
                const auto q=sketch.local_point(point);const auto origin=sketch.world_point(0,q[1]);
                require(std::abs(std::hypot(point.x-origin.x,point.y-origin.y,point.z-origin.z)-p[0])<1e-7,
                    "Thin revolution sweep does not retain its native radius");
            }
        }
        require(sweeps==8&&actual==expected,"Thin revolution omitted or duplicated an authored swept junction");
        auto results=engine.evaluate_history(doc.kernel_operations());require(results.back().calculation_errors.empty(),"Thin revolution failed");
        auto body=results.back();engine.ensure_body_properties(body);
        const double area=(20+2*outside)*(12+2*outside)-(20-2*inside)*(12-2*inside);
        require(std::abs(body.volume-area*15*std::acos(-1.)/2)<1e-5,"Thin revolution volume disagrees with the independent section moment");++cases;
    }
    std::cout<<"Thin revolution: "<<cases<<" plane/mode/direction cases, eight swept junctions and independent volume passed"<<std::endl;
}
void review_fillets(const kernel::OcctKernel& engine,const std::filesystem::path& file) {
    std::vector<kernel::BodyResult> calculated;auto source=document::PartDocument::load(file,&calculated);
    workspace::Workspace regenerated;regenerated.add_part(source,calculated);regenerated.activate(source.document_id);
    static_cast<void>(workspace::regenerate_part(regenerated,engine,source.document_id));
    source=regenerated.open_part(source.document_id)->session.document();
    calculated=regenerated.open_part(source.document_id)->session.calculated_boundaries();
    require(calculated.back().calculation_errors.empty(),"Actual review document still fails explicit regeneration");
    const auto review_file=file.parent_path()/"review-regenerated.prtz";
    source.save(review_file,calculated);
    std::cout<<"Regenerated private review: "<<review_file.generic_string()<<std::endl;
    const auto rounded=std::ranges::find(source.history,std::string("Vytažení 007"),&document::HistoryContainer::name);
    require(rounded!=source.history.end(),"Actual rounded review feature 007 is missing");
    const auto profile=std::ranges::find(source.sketches,rounded->feature.sketch_id,&sketcher::Sketch::id);
    require(profile!=source.sketches.end()&&profile->corner_radii.size()==1,"Actual rounded review source is not the reported one-corner profile");
    for(unsigned corners:{1u,2u})for(double radius:{6.,8.619847297682325,10.}) {
        auto edited=*profile;const auto corner=edited.corner_radii.front();
        static_cast<void>(edited.add_corner_fillet(corner.first_segment_id,corner.second_segment_id,radius));
        if(corners==2)static_cast<void>(edited.add_corner_fillet(edited.segments[0].id,edited.segments[3].id,radius));
        const auto before_document=regenerated.open_part(source.document_id)->session.document();
        const auto before=before_document.serialized();
        workspace::commit_profile(regenerated,engine,source.document_id,*rounded,workspace::ProfileEditMode::Replace,edited);
        auto& session=regenerated.open_part(source.document_id)->session;
        require(session.calculated_boundaries().back().calculation_errors.empty(),"Actual 007 one/two-corner radius edit failed");
        const auto after=session.document().serialized();
        if(after!=before) {
            require(session.undo(),"Actual 007 radius edit Undo is unavailable");
            // New corner dimensions reserve identifiers permanently, including
            // across Undo; every authored geometric field must restore exactly.
            auto expected=before_document;expected.dimension_identifiers.retain(session.document().dimension_identifiers);
            require(session.document().serialized()==expected.serialized(),"Actual 007 radius edit Undo lost its source definition");
            require(session.redo()&&session.document().serialized()==after,"Actual 007 radius edit Redo changed its source definition");
        }
        const auto saved=file.parent_path()/("review-007-"+std::to_string(corners)+"-"+std::to_string(radius)+".prtz");
        session.document().save(saved,session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened;const auto loaded=document::PartDocument::load(saved,&reopened);
        require(loaded.serialized()==after&&reopened.back().calculation_errors.empty(),"Actual 007 save/reopen changed or lost the valid rounded profile");
    }
    std::cout<<"Actual 007: six one/two-corner radius edits, Undo/Redo and native save/reopen passed"<<std::endl;
    for(const auto& feature:source.history)if(feature.feature_kind==document::FeatureKind::Feature&&
            feature.feature.result_type==document::ProfileResultType::Thin) {
        const auto* body=source.body_owner_for_object(feature.id);require(body,"Review Thin feature has no Body");
        auto doc=source;auto graph=doc.body_history;graph.activate(body->scope.id);
        const auto entry=std::ranges::find(body->entries,feature.id,&document::PartHistoryEntry::id);
        graph.set_history_cursor(body->scope.id,static_cast<std::size_t>(entry-body->entries.begin())+1);
        doc.set_body_history(graph);
        document::DocumentSession snapshot(doc,calculated);
        const auto* input=workspace::calculated_operation_input(snapshot,{});require(input,"Review Thin input missing");
        const auto sketch=std::ranges::find(doc.sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
        require(sketch!=doc.sketches.end(),"Review Thin profile missing");
        if(sketch->corner_radii.empty()&&sketch->segments.size()==4) {
            const auto wire=app::authored_feature_wire(snapshot,feature.id);
            tests::verify_rectangular_thin_start(doc,feature,wire);
        }
        std::vector<kernel::ViewerEdge> candidates;
        for(const auto& edge:input->mesh.edges)if(edge.reference.owner_id==feature.id&&longitudinal(edge,sketch->normal()))
            candidates.push_back(edge);
        std::cout<<"Review "<<feature.name<<" longitudinal input edges="<<candidates.size()<<std::endl;
        if(candidates.size()<2){std::cout<<"Review has no calculated material edges; input errors:";
            for(const auto& [id,error]:input->calculation_errors)std::cout<<' '<<id<<": "<<error;
            std::cout<<'\n';continue;}
        for(double radius:{.1,.25,.4,1.}) {
            workspace::Workspace live;live.add_part(doc,calculated);live.activate(doc.document_id);
            auto treatment=document::PartDocument::create_fillet_container({candidates[0].reference,candidates[1].reference});
            treatment.edge_treatment.primary_size=treatment.edge_treatment.secondary_size=radius;
            treatment.edge_treatment.routes={{candidates[0].reference},{candidates[1].reference}};
            const auto before=live.open_part(doc.document_id)->session.document().serialized();
            bool committed=false;
            try {
                committed=workspace::commit_edge_treatment(live,engine,doc.document_id,treatment,workspace::EdgeTreatmentEditMode::Create);
            }catch(const std::exception& error) {
                require(live.open_part(doc.document_id)->session.document().serialized()==before,"Rejected review fillet mutated its document");
                std::cout<<"Review "<<feature.name<<" two-edge radius="<<radius<<" rejected: "<<error.what()<<'\n';
                throw;
            }
            require(committed,"Review fillet did not commit");
            const auto& errors=live.open_part(doc.document_id)->session.calculated_boundaries().back().calculation_errors;
            std::cout<<"Review "<<feature.name<<" two-edge radius="<<radius<<" errors="<<errors.size()<<'\n';
            for(const auto& [id,error]:errors)std::cout<<"  "<<(id==treatment.id?"FILLET":"OTHER")<<": "<<error<<'\n';
            require(errors.empty(),"Actual review two-edge fillet retained a calculation error");

        }
    }
}
void rounded_profile_matrix(const kernel::OcctKernel& engine) {
    unsigned cases{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(bool reversed:{false,true})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(unsigned corners:{1u,2u}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(0,0,30,40));const auto segments=sketch.segments;
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.result_type=document::ProfileResultType::Thin;feature.feature.thin_mode=mode;
        feature.feature.thin_thickness=1;feature.feature.sides[0].length=20;feature.feature.reversed_sides=reversed;
        doc.history={feature};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Rounded Thin profile"));
        graph.insert({document::PartHistoryKind::Feature,feature.id});doc.set_body_history(graph);
        std::vector<std::string> corner_ids;
        for(double radius:{8.619847297682325,6.,10.}) {
            static_cast<void>(sketch.add_corner_fillet(segments[0].id,segments[1].id,radius));
            if(corners==2)static_cast<void>(sketch.add_corner_fillet(segments[2].id,segments[3].id,radius));
            if(corner_ids.empty())for(const auto& corner:sketch.corner_radii)corner_ids.push_back(corner.id);
            else for(std::size_t i=0;i<corner_ids.size();++i)require(sketch.corner_radii[i].id==corner_ids[i],"Radius edit replaced a source corner identity");
            doc.sketches={sketch};doc.resolve_constructions();
            const auto wire=doc.feature_preview_edges(feature,{});unsigned rails{};
            std::set<std::array<long long,2>> actual,expected_points;
            for(const auto& edge:wire)if(longitudinal(edge,doc.sketches.front().normal()))++rails;
            require(rails==2*(4+corners),"Rounded Thin preview omitted or duplicated native tangent-junction rails");
            auto results=engine.evaluate_history(doc.kernel_operations());
            require(!results.empty()&&results.back().calculation_errors.empty(),"A valid rounded Thin profile failed to calculate");
            auto measured=results.back();engine.ensure_body_properties(measured);
            const double outside=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?1:.5;
            const double inside=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
            const auto quantize=[](double x,double y){return std::array{std::llround(x*1e7),std::llround(y*1e7)};};
            for(double offset:{-outside,inside}) {
                for(const auto& point:std::vector<std::array<double,2>>{{offset,offset},{30-radius,offset},{30-offset,radius},{30-offset,40-offset}})
                    expected_points.insert(quantize(point[0],point[1]));
                if(corners==1)expected_points.insert(quantize(offset,40-offset));
                else {expected_points.insert(quantize(radius,40-offset));expected_points.insert(quantize(offset,40-radius));}
            }
            for(const auto& edge:wire)if(longitudinal(edge,doc.sketches.front().normal())) {
                const auto p=doc.sketches.front().local_point(edge.points.front());actual.insert(quantize(p[0],p[1]));
            }
            require(actual==expected_points,"Rounded Thin rails do not match exact offset line/arc junctions");
            const auto area=[&](double offset){return (30+2*offset)*(40+2*offset)-corners*(1-std::acos(-1.)/4)*std::pow(radius+offset,2);};
            const auto expected=20*(area(outside)-area(-inside));
            require(std::abs(measured.volume-expected)<1e-5,"Rounded Thin volume disagrees with the independent line/arc area integral");
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());++cases;
        }
    }
    std::cout<<"Rounded Thin profiles: "<<cases<<" native volume/rail/identity/radius-edit cases passed"<<std::endl;
}

}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try{kernel::OcctKernel engine;rectangular_matrix(engine);revolution_matrix(engine);rounded_profile_matrix(engine);
        if(const auto* file=std::getenv("ZIMA_THIN_REVIEW_FILE"))review_fillets(engine,std::filesystem::u8path(file));
        return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
