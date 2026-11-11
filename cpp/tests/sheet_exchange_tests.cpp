#include <zima/kernel/sheet_exchange.hpp>
#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step_model.hpp>
#include <QTemporaryDir>
#include <zima/document/flat.hpp>
#include <zima/document/profile_status.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/metadata.hpp>
#include <zima/document/solid_state_calculation.hpp>
#include <zima/document/sheet_rip.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/line_bend_operations.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <iostream>
#include <map>
#include <array>
#include <cmath>
#include <chrono>
#include <numbers>
#include <iomanip>
#include "profile_solid_fixture.hpp"

namespace {
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-5){if(std::abs(a-b)>tolerance)throw std::runtime_error("Mismatch: "+std::to_string(a)+" versus "+std::to_string(b));}
double measured_volume(const kernel::OcctKernel& engine,const kernel::BodySnapshot& snapshot) {
    auto body=snapshot.get();engine.ensure_body_properties(body);return body.volume;
}
void rip_contract(kernel::OcctKernel& engine) {
    // RIP consumes every exact ordinary native curve while construction
    // geometry remains inert. Open curves split the sheet into branches;
    // closed curves retain both the inside and outside material regions.
    auto exact=sketcher::Sketch::create_default();
    const std::vector<std::string> expected{
        exact.add_segment(0,1,10,1),
        exact.add_circle(3,3,1),
        exact.add_arc(5,3,6,3,5,4),
        exact.add_ellipse(7,3,8.5,3,7,3.5),
        exact.add_elliptical_arc(2,6,3.5,6,2,6.5,3.5,6,2,6.5),
        exact.add_bspline({{5,6},{6,7},{8,5},{9,6}},3)};
    static_cast<void>(exact.add_segment(0,7,10,7,1e-6,true));
    if(exact.segments.size()!=2||exact.circles.size()!=1||
            exact.arcs.size()!=1||exact.ellipses.size()!=1||
            exact.elliptical_arcs.size()!=1||exact.bsplines.size()!=1)
        throw std::runtime_error("Invalid native RIP curve fixture");
    const auto request=document::sheet_rip_request({&exact},.05);
    if(request.curves.size()!=expected.size()) {
        std::string actual_ids;
        for(const auto& curve:request.curves)
            actual_ids+=" "+curve.source_id;
        throw std::runtime_error(
            "RIP did not consume every ordinary native Sketch curve exactly once: expected "+
            std::to_string(expected.size())+", actual "+
            std::to_string(request.curves.size())+"; ids:"+actual_ids);
    }
    for(const auto& id:expected)
        check(std::ranges::count(request.curves,id,
                &kernel::SheetRipCurve::source_id)==1,
            "RIP changed or omitted a native Sketch curve identity");
    check(std::ranges::none_of(request.curves,[](const auto& curve) {
            return curve.source_id.empty();
        }),"RIP emitted an unidentified native Sketch curve");
    bool duplicate_rejected{};
    try {static_cast<void>(document::sheet_rip_request(
        {&exact,&exact},.05));}
    catch(const std::invalid_argument&) {duplicate_rejected=true;}
    check(duplicate_rejected,
        "RIP accepted the same Sketch reference more than once");

    const auto split=[&](sketcher::Sketch sketch,const char* owner) {
        const auto rip=document::sheet_rip_request({&sketch},.01);
        test::ProfilePrism stock{10,8,6};
        const auto result=engine.evaluate_history({
            {std::string(owner)+":stock",stock},
            {owner,rip}}).back();
        check(result.calculation_errors.empty(),
            "RIP partition produced a calculation error");
        near(measured_volume(engine,result),480,.0001);
        const auto shape=kernel::read_calculated_body_shape(result);
        unsigned solids{};
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())
            ++solids;
        check(solids>=2,
            "RIP did not create independent material branches");
    };
    auto open=sketcher::Sketch::create_default();
    static_cast<void>(open.add_segment(0,4,10,4));
    split(std::move(open),"rip-open");
    auto closed=sketcher::Sketch::create_default();
    static_cast<void>(closed.add_circle(5,4,2));
    split(std::move(closed),"rip-closed");
}
void rip_conversion_contract(kernel::OcctKernel& engine) {
    auto part=document::PartDocument::create_default();
    const auto body=part.body_history.create_body("RIP source");
    auto flat=document::PartDocument::create_sketch_container();
    flat.feature_kind=document::FeatureKind::Flat;
    flat.flat.thickness_override=true;
    flat.flat.thickness=2;
    flat.flat.direction=document::ExtrusionDirection::Reverse;
    auto profile=sketcher::Sketch::create_default();
    profile.owner_container_id=flat.id;
    flat.flat.sketch_id=profile.id;
    static_cast<void>(profile.add_rectangle(-50,-40,50,40));
    part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);
    part.history.push_back(flat);
    part.sketches.push_back(profile);

    auto rip_feature=document::PartDocument::create_sketch_container();
    rip_feature.feature_kind=document::FeatureKind::Sketch;
    auto rip=sketcher::Sketch::create_default();
    rip.owner_container_id=rip_feature.id;
    static_cast<void>(rip.add_segment(-50,0,50,0));
    part.insert_history_entry(document::PartHistoryKind::Feature,
        rip_feature.id);
    part.history.push_back(rip_feature);
    part.sketches.push_back(rip);

    const auto cache=workspace::calculate_part_with_resolved_references(
        engine,part);
    const auto& faces=cache.back().body_outputs.at(body)->mesh.
        original_references.triangle_references;
    const auto seed=std::ranges::find_if(faces,[&](const auto& face) {
        return face.owner_id==flat.id&&face.sheet_role==
                kernel::SheetFaceRole::SideA&&face.surface&&
            face.surface->kind==kernel::SurfaceGeometry::Kind::Plane;
    });
    check(seed!=faces.end(),"RIP conversion fixture has no source skin");
    const double source_volume=measured_volume(engine,
        cache.back().body_outputs.at(body));
    const auto converted=workspace::prepare_sheet_from_body(part,cache,
        *seed,2,engine,true,{},std::vector<std::string>{rip.id});
    const auto* boundary=converted.document.find_container(
        converted.container_id);
    check(boundary&&boundary->sheet_from_body.rip_sketches==
            std::vector<std::string>{rip.id},
        "Sheet from Body did not persist its RIP Sketch reference");
    check(converted.created.size()==2&&
            std::ranges::count_if(boundary->sheet_from_body.child_bindings,
                [](const auto& binding){return binding.root;})==2,
        "RIP conversion did not author both material root regions");
    check(!converted.created.empty()&&
            converted.calculated.back().calculation_errors.empty(),
        "Sheet from Body did not calculate through its RIP Sketch");
    near(measured_volume(engine,
        converted.calculated.back().body_outputs.at(body)),source_volume,.001);
    auto reopened=document::PartDocument::from_serialized(
        converted.document.serialized());
    const auto cold=workspace::calculate_part_with_resolved_references(
        engine,reopened);
    check(cold.back().calculation_errors.empty(),
        "RIP Sheet from Body failed cold regeneration");
    near(measured_volume(engine,cold.back().body_outputs.at(body)),
        source_volume,.001);
}
void line_bend_contract(kernel::OcctKernel& engine) {
    auto part=document::PartDocument::create_default();
    const auto body=part.body_history.create_body("Line Bend sheet");
    auto flat=document::PartDocument::create_sketch_container();
    flat.feature_kind=document::FeatureKind::Flat;
    flat.flat.thickness_override=true;
    flat.flat.thickness=2;
    flat.flat.direction=document::ExtrusionDirection::Reverse;
    auto profile=sketcher::Sketch::create_default();
    profile.owner_container_id=flat.id;
    flat.flat.sketch_id=profile.id;
    static_cast<void>(profile.add_rectangle(-50,-40,50,40));
    part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);
    part.history.push_back(flat);
    part.sketches.push_back(profile);

    auto bend=document::PartDocument::create_sketch_container();
    bend.feature_kind=document::FeatureKind::LineBend;
    bend.line_bend.radius=5;
    bend.line_bend.angle_degrees=90;
    bend.line_bend.thickness_override=true;
    bend.line_bend.thickness=2;
    bend.line_bend.k_factor_override=true;
    bend.line_bend.k_factor=.5;
    bend.line_bend.fixed_side=1;
    auto line=sketcher::Sketch::create_default();
    line.owner_container_id=bend.id;
    bend.line_bend.sketch_id=line.id;
    bend.line_bend.segment_id=line.add_segment(-50,0,50,0);
    part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);
    part.history.push_back(bend);
    part.sketches.push_back(line);

    const auto calculated=workspace::calculate_part_with_resolved_references(
        engine,part);
    check(calculated.back().calculation_errors.empty(),
        "Line Bend produced a calculation error");
    near(measured_volume(engine,calculated.back().body_outputs.at(body)),
        16000,.01);
    const auto& mesh=calculated.back().body_outputs.at(body)->mesh;
    check(std::ranges::any_of(mesh.triangle_references,[&](const auto& face) {
            return face.owner_id==bend.id&&face.surface&&
                face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;
        }),"Line Bend did not publish its cylindrical material region");
    {
        workspace::Workspace live;live.add_part(part);
        auto* state=live.open_part(part.document_id);
        state->session.update_calculated_boundaries(calculated);
        const auto revision=state->session.revision();
        const auto* cache=state->session.calculated_boundaries().data();
        check(!workspace::commit_line_bend(live,engine,part.document_id,
                bend,line)&&state->session.revision()==revision&&
                state->session.calculated_boundaries().data()==cache,
            "Unchanged Line Bend recalculated geometry or created Undo");
        auto invalid=line;
        static_cast<void>(invalid.add_segment(-50,10,50,10));
        bool rejected{};
        try {static_cast<void>(workspace::commit_line_bend(live,engine,
            part.document_id,bend,std::move(invalid)));}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected&&state->session.revision()==revision&&
                state->session.calculated_boundaries().data()==cache,
            "Invalid Line Bend partly changed history or geometry");
    }
    auto reopened=document::PartDocument::from_serialized(part.serialized());
    const auto* restored=reopened.find_container(bend.id);
    check(restored&&restored->feature_kind==document::FeatureKind::LineBend&&
            restored->line_bend==bend.line_bend,
        "Line Bend parameters changed during native round trip");
    const auto cold=workspace::calculate_part_with_resolved_references(
        engine,reopened);
    check(cold.back().calculation_errors.empty(),
        "Line Bend failed cold regeneration");
    near(measured_volume(engine,cold.back().body_outputs.at(body)),16000,.01);

    const auto verify_variant=[&](int fixed_side,bool reverse,
            bool inner_tab) {
        auto variant=document::PartDocument::create_default();
        const auto variant_body=variant.body_history.create_body(
            inner_tab?"Inner Line Bend tab":"Line Bend direction variant");
        auto variant_flat=document::PartDocument::create_sketch_container();
        variant_flat.feature_kind=document::FeatureKind::Flat;
        variant_flat.flat.thickness_override=true;
        variant_flat.flat.thickness=2;
        variant_flat.flat.direction=document::ExtrusionDirection::Reverse;
        auto variant_profile=sketcher::Sketch::create_default();
        variant_profile.owner_container_id=variant_flat.id;
        variant_flat.flat.sketch_id=variant_profile.id;
        if(inner_tab) {
            const std::array<std::array<double,2>,8> outline{{
                {-50,-40},{50,-40},{50,0},{20,0},
                {20,40},{-20,40},{-20,0},{-50,0}}};
            for(std::size_t i=0;i<outline.size();++i) {
                const auto& first=outline[i];
                const auto& last=outline[(i+1)%outline.size()];
                static_cast<void>(variant_profile.add_segment(
                    first[0],first[1],last[0],last[1]));
            }
        } else static_cast<void>(variant_profile.add_rectangle(
            -50,-40,50,40));
        variant.insert_history_entry(document::PartHistoryKind::Feature,
            variant_flat.id);
        variant.history.push_back(variant_flat);
        variant.sketches.push_back(variant_profile);

        auto variant_bend=document::PartDocument::create_sketch_container();
        variant_bend.feature_kind=document::FeatureKind::LineBend;
        variant_bend.line_bend.radius=5;
        variant_bend.line_bend.angle_degrees=75;
        variant_bend.line_bend.thickness_override=true;
        variant_bend.line_bend.thickness=2;
        variant_bend.line_bend.k_factor_override=true;
        variant_bend.line_bend.k_factor=.5;
        variant_bend.line_bend.fixed_side=fixed_side;
        variant_bend.line_bend.reverse=reverse;
        auto variant_line=sketcher::Sketch::create_default();
        variant_line.owner_container_id=variant_bend.id;
        variant_bend.line_bend.sketch_id=variant_line.id;
        variant_bend.line_bend.segment_id=inner_tab?
            variant_line.add_segment(-20,0,20,0):
            variant_line.add_segment(-50,0,50,0);
        variant.insert_history_entry(document::PartHistoryKind::Feature,
            variant_bend.id);
        variant.history.push_back(variant_bend);
        variant.sketches.push_back(variant_line);

        const auto result=workspace::calculate_part_with_resolved_references(
            engine,variant);
        check(result.back().calculation_errors.empty(),inner_tab?
            "Finite inner Line Bend produced a calculation error":
            "Line Bend side/direction variant produced a calculation error");
        near(measured_volume(engine,
            result.back().body_outputs.at(variant_body)),
            inner_tab?11200.:16000.,.2);
        check(std::ranges::any_of(
                result.back().body_outputs.at(variant_body)->mesh.
                    triangle_references,
                [&](const auto& face) {
                    return face.owner_id==variant_bend.id&&face.surface&&
                        face.surface->kind==
                            kernel::SurfaceGeometry::Kind::Cylinder;
                }),inner_tab?
            "Finite inner Line Bend has no cylindrical material region":
            "Line Bend side/direction variant has no cylindrical region");
    };
    for(const int fixed_side:{-1,1})for(const bool reverse:{false,true})
        verify_variant(fixed_side,reverse,false);
    // The segment endpoints lie on the two re-entrant edges of a tongue.
    // This is a finite internal bend: material outside its along interval
    // remains fixed, while the tongue is the moving continuation.
    verify_variant(-1,false,true);
    verify_variant(-1,true,true);
}
void contract(bool include_oblique_corners = true) {
    kernel::OcctKernel kernel;
    rip_contract(kernel);
    rip_conversion_contract(kernel);
    line_bend_contract(kernel);
    {
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body("Rounded solid");
        auto box=test::rectangular_feature(part,{100,80,20});
        part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        std::vector<kernel::EdgeReference> edges;
        for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
            if(std::ranges::any_of(edge.points,[](auto p){return std::hypot(p.x+50,p.y+40,p.z+10)<1e-6;}))edges.push_back(edge.reference);
        check(edges.size()==3,"Rounded corner fixture lacks three joining edges");
        auto fillet=document::PartDocument::create_fillet_container(edges);fillet.edge_treatment.primary_size=4;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);part.history.push_back(fillet);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache,policy);
        const auto& references=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::max_element(references,{},[](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Plane?f.measured_area.value_or(0):0;});
        const auto before=part.serialized();bool rejected=false;
        std::string rejection;
        try {
            const auto conversion=workspace::prepare_sheet_from_body(
                part,cache,*seed,1,kernel);
            rejection="created="+std::to_string(conversion.created.size())+
                " skipped="+std::to_string(conversion.skipped);
        }
        catch(const std::invalid_argument& error) {
            rejection=error.what();
            rejected=rejection==
                "Sheet conversion cannot reproduce this surface transition.";
        }
        if(!rejected||part.serialized()!=before)
            throw std::runtime_error(
                "Three-edge corner blend was silently converted or modified: "+
                (rejection.empty()?"no rejection":rejection));
    }
    {
        // Three rounded perimeter edges reproduce the real Sheet-from-Body
        // branch layout: two outside bends meet one neighbour and the middle
        // bend meets neighbours at both ends. Each meeting extension must stop
        // at the thickness- and angle-corrected endpoint (one thickness for
        // this right-angle fixture).
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body("Three rounded sheet branches");
        constexpr double width=355.2950439453125;
        constexpr double height=154.76907755780147;
        constexpr double bottom_y=-10.449853897094727;
        constexpr double top_y=166.9654541015625;
        auto source_profile=sketcher::Sketch::create_default();
        // Keep the perimeter clockwise and the opening off-centre.  After a
        // STEP round trip this makes the source-face entry run opposite to
        // one native Bend End. Conversion must select the matching endpoint
        // explicitly; relying on the kernel edge order misplaces the holed
        // child Flat at the far end of its attachment.
        const std::vector<std::string> rectangle{
            source_profile.add_segment(width/2,bottom_y,-width/2,bottom_y),
            source_profile.add_segment(-width/2,bottom_y,-width/2,top_y),
            source_profile.add_segment(-width/2,top_y,width/2,top_y),
            source_profile.add_segment(width/2,top_y,width/2,bottom_y)};
        static_cast<void>(source_profile.add_circle(
            -width/4,bottom_y+40,5));
        const auto bottom_segment=std::ranges::find(
            source_profile.segments,rectangle.front(),
            &sketcher::SketchSegment::id);
        check(bottom_segment!=source_profile.segments.end(),
            "Three-branch fixture lost its bottom profile segment");
        static_cast<void>(source_profile.add_symmetric_constraint(
            bottom_segment->first_point_id,bottom_segment->second_point_id,
            "sketch_axis:y"));
        auto box=document::PartDocument::create_feature_container(
            source_profile.id);
        source_profile.owner_container_id=box.id;
        box.feature.sides[0].length=height;
        const auto origin=body+":origin";
        const auto frame_reference=[&](const char* plane,const char* role,
                bool orientation_only) {
            document::ConstructionReference reference;
            reference.owner_id=origin;
            reference.semantic_key=std::string("origin:plane:")+plane;
            reference.supports_offset=true;
            reference.orientation_drives_rotation=true;
            reference.orientation_role=role;
            reference.orientation_only=orientation_only;
            return reference;
        };
        box.placement.references={
            frame_reference("xz","front",false),
            frame_reference("xy","top",false),
            frame_reference("yz","top",false),
            frame_reference("xz","front",true),
            frame_reference("xy","top",true)};
        document::set_sheet_metal_defaults(part,
            {1.,0.318309886184});
        part.sketches.push_back(std::move(source_profile));
        part.insert_history_entry(document::PartHistoryKind::Feature,box.id);
        part.history.push_back(box);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& box_edges=cache.back().body_outputs.at(body)->mesh.edges;
        double bottom=INFINITY;
        for(const auto& edge:box_edges)for(const auto point:edge.points)
            bottom=std::min(bottom,point.z);
        std::vector<kernel::EdgeReference> perimeter;
        for(const auto& edge:box_edges)
            if(edge.points.size()==2&&std::ranges::all_of(edge.points,
                    [&](const auto& point){return std::abs(point.z-bottom)<1e-6;})&&
                    !std::ranges::all_of(edge.points,[&](const auto& point) {
                        return std::abs(point.y-top_y)<1e-6;
                    }))
                perimeter.push_back(edge.reference);
        check(perimeter.size()==3,
            "Three-branch fixture lacks its U-shaped bottom edge set");
        auto fillet=document::PartDocument::create_fillet_container(perimeter);
        fillet.edge_treatment.routes.clear();
        fillet.edge_treatment.route_end_faces.clear();
        for(const auto& edge:perimeter) {
            fillet.edge_treatment.routes.push_back({edge});
            fillet.edge_treatment.route_end_faces.emplace_back();
        }
        fillet.edge_treatment.primary_size=11;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);
        part.history.push_back(fillet);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(
            kernel,part,&cache,policy);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[&](const auto& face) {
            return face.surface&&face.surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane&&
                    std::abs(face.surface->origin.z-bottom)<1e-6&&
                    face.surface->axis.z<-.999999&&!face.surface->reversed;
        });
        check(seed!=faces.end()&&seed->surface&&
                seed->surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(seed->surface->origin.z-bottom)<1e-6,
            "Three-branch fixture lacks its rounded bottom face");
        const auto converted=workspace::prepare_sheet_from_body(
            part,cache,*seed,1,kernel);
        check(converted.created.size()==7&&converted.skipped==0,
            "Three rounded edges did not create four Flats and three Bends");
        const auto* conversion=converted.document.find_container(
            converted.container_id);
        check(conversion&&conversion->sheet_from_body.child_bindings.size()==
                    converted.created.size()&&
                conversion->sheet_from_body.child_bindings.front().root,
            "Sheet conversion did not retain one source binding per child");
        for(const auto& binding:
                conversion->sheet_from_body.child_bindings) {
            check(!binding.faces.empty()&&!binding.edges.empty()&&
                    binding.references_valid&&!binding.detached,
                "Sheet conversion source binding is incomplete");
            if(!binding.bend)continue;
            const auto has_role=[&](document::SheetFromBodyFaceRole role) {
                return std::ranges::any_of(binding.faces,
                    [&](const auto& face){return face.role==role;});
            };
            check((has_role(document::SheetFromBodyFaceRole::BendInner)||
                    has_role(document::SheetFromBodyFaceRole::BendOuter))&&
                    has_role(document::SheetFromBodyFaceRole::Adjacent),
                "Converted Bend did not retain its radius and adjacent source faces");
            check(std::ranges::any_of(binding.edges,[](const auto& edge) {
                    return edge.role==
                        document::SheetFromBodyEdgeRole::Entry;
                })&&std::ranges::any_of(binding.edges,[](const auto& edge) {
                    return edge.role==
                        document::SheetFromBodyEdgeRole::Exit;
                }),
                "Converted Bend did not retain its source entry and exit edges");
        }
        std::size_t double_meeting{},single_meeting{},bends{};
        std::string middle_bend;
        for(const auto& id:converted.created) {
            const auto* feature=converted.document.find_container(id);
            if(feature->feature_kind!=document::FeatureKind::Bend)continue;
            ++bends;
            const auto extension=document::bend_profile_extensions(*feature);
            const auto first=std::abs(extension[0]);
            const auto last=std::abs(extension[1]);
            const bool first_zero=first<1e-8,last_zero=last<1e-8;
            const bool first_ten=std::abs(first-10)<1e-8;
            const bool last_ten=std::abs(last-10)<1e-8;
            if(first_ten&&last_ten) {
                ++double_meeting;middle_bend=id;
            } else if((first_zero&&last_ten)||(last_zero&&first_ten))
                ++single_meeting;
            else throw std::runtime_error(
                "Sheet meeting-end setback is not radius 11 minus thickness 1");
        }
        check(bends==3&&double_meeting==1&&single_meeting==2,
            "Converted Bend meeting ends did not use the 11 - 1 setback");
        std::size_t clipped_flats{};
        for(const auto& id:converted.created) {
            const auto* flat=converted.document.find_container(id);
            if(!flat||flat->feature_kind!=document::FeatureKind::Flat)
                continue;
            const auto sketch=std::ranges::find(converted.document.sketches,
                flat->flat.sketch_id,&sketcher::Sketch::id);
            if(sketch==converted.document.sketches.end())continue;
            const auto first=std::ranges::find(sketch->external_references,
                sketch->id+":flat-attachment:first",
                &sketcher::SketchExternalReference::id);
            const auto last=std::ranges::find(sketch->external_references,
                sketch->id+":flat-attachment:last",
                &sketcher::SketchExternalReference::id);
            if(first==sketch->external_references.end()||
                    last==sketch->external_references.end())continue;
            check(first->cached_points.size()==1&&
                    last->cached_points.size()==1,
                "Converted Flat lost its Bend endpoint stations");
            ++clipped_flats;
        }
        check(clipped_flats==3,
            "Three-branch conversion did not clip every Flat after its Bend");
        check(converted.calculated.back().calculation_errors.empty()&&
                converted.calculated.back().body_outputs.at(body)->has_solid(),
            "Three-branch conversion produced no solid sheet");

        // Reimport the same analytic source as STEP. Unlike an editable
        // native solid, an imported body is finished supplier geometry: its
        // endpoint spans must be preserved verbatim rather than shortened a
        // second time by ZIMA's native-solid clearance rule.
        QTemporaryDir imported_fixture(
            QStringLiteral("build/three-sheet-step-XXXXXX"));
        check(imported_fixture.isValid(),
            "Cannot isolate imported three-branch fixture");
        const auto step_path=std::filesystem::u8path(
            imported_fixture.path().toStdString())/"finished-sheet.step";
        kernel.export_step(std::vector<kernel::PlacedBody>{{
            cache.back().body_outputs.at(body).get(),{},{}}},
            step_path.string());
        auto imported=interchange::import_step_part(
            document::PartDocument::create_default(),{},step_path);
        const auto& imported_faces=
            imported.calculated.back().mesh.original_references.
                triangle_references;
        const auto imported_seed=std::ranges::find_if(
            imported_faces,[&](const auto& face) {
                if(!face.surface||face.surface->kind!=
                        kernel::SurfaceGeometry::Kind::Plane)
                    return false;
                const auto& surface=*face.surface;
                const double outward_x=surface.axis.x*
                    (surface.reversed?-1:1);
                return std::abs(surface.origin.x+width/2)<1e-6&&
                    outward_x<-.999999;
            });
        check(imported_seed!=imported_faces.end()&&imported_seed->surface&&
                imported_seed->surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane,
            "Imported finished sheet lacks a planar seed");
        const auto reconverted=workspace::prepare_sheet_from_body(
            imported.document,imported.calculated,*imported_seed,1,kernel);
        check(reconverted.created.size()==7&&reconverted.skipped==0,
            "Imported finished sheet did not reproduce its seven branches");
        const document::HistoryContainer* holed_flat=nullptr;
        std::size_t imported_flat_circles{};
        for(const auto& id:reconverted.created) {
            const auto* feature=reconverted.document.find_container(id);
            if(!feature||feature->feature_kind!=document::FeatureKind::Flat)
                continue;
            const auto sketch=std::ranges::find(reconverted.document.sketches,
                feature->flat.sketch_id,&sketcher::Sketch::id);
            check(sketch!=reconverted.document.sketches.end(),
                "Imported STEP Flat lost its profile Sketch");
            imported_flat_circles+=sketch->circles.size();
            if(!sketch->circles.empty())holed_flat=feature;
        }
        check(imported_flat_circles==1&&holed_flat,
            "Imported STEP full-circle opening was not retained as one native Sketch circle");
        const auto reconverted_geometry=
            workspace::construction_reference_source_geometry(
                reconverted.calculated);
        unsigned reverse_anchors{};
        for(const auto& id:reconverted.created) {
            const auto* feature=reconverted.document.find_container(id);
            if(!feature||feature->placement.references.size()<3)continue;
            const auto* edge=document::current_sheet_attachment_edge(
                feature->placement.references,reconverted_geometry);
            if(!edge||edge->edge_treatment_endpoint_references.size()!=2)
                continue;
            const auto& anchor=feature->placement.references[2];
            const auto& endpoint=
                edge->edge_treatment_endpoint_references.back();
            reverse_anchors+=anchor.owner_id==endpoint.owner_id&&
                anchor.semantic_key==endpoint.semantic_key&&
                anchor.instance_path==endpoint.instance_path;
        }
        check(reverse_anchors>0,
            "Imported STEP conversion ignored a reversed attachment endpoint");
        constexpr std::array imported_branch_order{
            document::FeatureKind::Flat,
            document::FeatureKind::Bend,document::FeatureKind::Flat,
            document::FeatureKind::Bend,document::FeatureKind::Flat,
            document::FeatureKind::Bend,document::FeatureKind::Flat};
        for(std::size_t index=0;index<imported_branch_order.size();++index) {
            const auto* feature=reconverted.document.find_container(
                reconverted.created[index]);
            check(feature&&feature->feature_kind==imported_branch_order[index],
                "Imported STEP conversion did not complete each Bend branch before starting the next branch");
        }
        std::size_t imported_elevens{},imported_zeros{};
        for(const auto& id:reconverted.created) {
            const auto* feature=reconverted.document.find_container(id);
            if(feature->feature_kind!=document::FeatureKind::Bend)continue;
            for(const auto extension:document::bend_profile_extensions(*feature)) {
                if(std::abs(extension)<1e-8)++imported_zeros;
                else if(std::abs(std::abs(extension)-11)<1e-8)
                    ++imported_elevens;
                else throw std::runtime_error(
                    "Imported STEP meeting end received a second setback");
            }
        }
        check(imported_elevens==4&&imported_zeros==2,
            "Imported STEP did not preserve its finished meeting endpoints");
        const auto complete_imported_sheet=[&](const auto& calculated) {
            return !calculated.empty()&&
                calculated.back().calculation_errors.empty()&&
                std::ranges::any_of(
                    calculated.back().body_outputs,
                    [](const auto& output){return output.second->has_solid();});
        };
        auto imported_cold_document=reconverted.document;
        const auto imported_cold=
            workspace::calculate_part_with_resolved_references(
                kernel,imported_cold_document);
        check(complete_imported_sheet(imported_cold),
            "Imported STEP conversion failed cold regeneration");
        const auto* converted_sheet_body=
            reconverted.document.body_owner_for_object(holed_flat->id);
        check(converted_sheet_body,
            "Imported holed Flat has no owning sheet Body");
        const auto sheet_output=reconverted.calculated.back().body_outputs.find(
            converted_sheet_body->scope.id);
        check(sheet_output!=reconverted.calculated.back().body_outputs.end(),
            "Imported holed sheet Body has no calculated output");
        const auto& sheet_geometry=
            sheet_output->second->mesh.original_references;
        std::vector<const kernel::ViewerEdge*> bend_candidates;
        for(const auto& edge:sheet_geometry.edges) {
            if(edge.reference.owner_id!=holed_flat->id||
                    kernel::sheet_edge_role(edge)!=
                        kernel::SheetEdgeRole::Boundary||
                    edge.edge_treatment_endpoint_references.size()!=2)
                continue;
            try {
                static_cast<void>(document::bend_sheet_references(edge,
                    edge.edge_treatment_endpoint_references.front()));
                bend_candidates.push_back(&edge);
            } catch(const std::invalid_argument&) {}
        }
        std::ranges::sort(bend_candidates,{},[](const auto* edge) {
            const auto delta=kernel::sheet_material::sub(
                edge->points.back(),edge->points.front());
            return kernel::sheet_material::dot(delta,delta);
        });
        std::ranges::reverse(bend_candidates);
        bool appended_bend{};
        std::string appended_bend_error;
        for(const auto* edge:bend_candidates)try {
            auto with_bend=reconverted.document;
            with_bend.body_history.activate(converted_sheet_body->scope.id);
            auto bend=document::PartDocument::create_sketch_container();
            bend.feature_kind=document::FeatureKind::Bend;
            bend.name="STEP circle follow-up Bend";
            bend.bend.sheet_attachment=true;
            const auto defaults=document::sheet_metal_defaults(with_bend);
            bend.bend.thickness=defaults.thickness_mm.value_or(1.);
            bend.bend.k_factor=defaults.k_factor;
            bend.bend.radius_follows_thickness=true;
            bend.bend.angle_degrees=90;
            bend.placement.references=document::bend_sheet_references(*edge,
                edge->edge_treatment_endpoint_references.front());
            auto profile=sketcher::Sketch::create_default();
            profile.owner_container_id=bend.id;
            profile.name=bend.name;
            bend.bend.sketch_id=profile.id;
            document::initialize_bend_start_profile(profile,40);
            with_bend.insert_history_entry(
                document::PartHistoryKind::Feature,bend.id);
            with_bend.history.push_back(bend);
            with_bend.sketches.push_back(profile);
            with_bend.resolve_constructions(sheet_geometry);
            workspace::PartCalculationPolicy bend_policy;
            bend_policy.reject_errors=true;
            kernel::OcctKernel bend_kernel;
            const auto bent=workspace::calculate_part_with_resolved_references(
                bend_kernel,with_bend,&reconverted.calculated,bend_policy);
            auto reopened=document::PartDocument::from_serialized(
                with_bend.serialized());
            kernel::OcctKernel bend_cold_kernel;
            const auto bent_cold=workspace::
                calculate_part_with_resolved_references(
                    bend_cold_kernel,reopened);
            check(bent_cold.back().calculation_errors.empty()&&
                    bent_cold.back().body_outputs.at(
                        converted_sheet_body->scope.id)->has_solid(),
                "Bend after a STEP circle failed cold regeneration");
            appended_bend=true;
            break;
        } catch(const std::exception& error) {
            appended_bend_error=error.what();
        }
        if(!appended_bend)throw std::runtime_error(
            "A native Bend could not follow a STEP Flat containing a split circular opening: "+
            appended_bend_error);
        QTemporaryDir imported_native_fixture(
            QStringLiteral("build/three-sheet-native-XXXXXX"));
        check(imported_native_fixture.isValid(),
            "Cannot isolate imported native sheet fixture");
        const auto imported_native_path=std::filesystem::u8path(
            imported_native_fixture.path().toStdString())/
                "imported-finished-sheet.prtz";
        reconverted.document.save(
            imported_native_path,reconverted.calculated);
        auto imported_reopened=document::PartDocument::load(
            imported_native_path);
        const auto imported_reopened_cold=
            workspace::calculate_part_with_resolved_references(
                kernel,imported_reopened);
        check(complete_imported_sheet(imported_reopened_cold)&&
                std::ranges::all_of(reconverted.created,[&](const auto& id) {
                    return imported_reopened.find_container(id)!=nullptr;
                }),
            "Imported STEP conversion failed save, reopen or cold regeneration");
        workspace::Workspace imported_live;
        const auto imported_document_id=imported.document.document_id;
        imported_live.add_part(imported.document,imported.calculated);
        workspace::commit_part_document(imported_live,imported_document_id,
            reconverted.document,reconverted.calculated);
        check(workspace::step_part_document_history(
                    imported_live,imported_document_id,false)&&
                std::ranges::none_of(reconverted.created,[&](const auto& id) {
                    return imported_live.open_part(imported_document_id)->
                        session.document().find_container(id)!=nullptr;
                }),
            "Imported STEP conversion Undo did not restore its source history");
        check(workspace::step_part_document_history(
                    imported_live,imported_document_id,true)&&
                std::ranges::all_of(reconverted.created,[&](const auto& id) {
                    return imported_live.open_part(imported_document_id)->
                        session.document().find_container(id)!=nullptr;
                })&&complete_imported_sheet(imported_live.open_part(
                    imported_document_id)->session.calculated_boundaries()),
            "Imported STEP conversion Redo did not restore its native sheet");

        workspace::Workspace live;
        const auto document_id=converted.document.document_id;
        live.add_part(converted.document,converted.calculated);
        const auto deletion_plan=workspace::plan_history_deletion(
            converted.document,middle_bend);
        check(deletion_plan.removed==std::set<std::string>{middle_bend}&&
                deletion_plan.affected.size()==1&&
                converted.document.find_container(
                    *deletion_plan.affected.begin())->feature_kind==
                    document::FeatureKind::Flat,
            "Middle-Bend deletion affected more than its directly attached Flat");
        workspace::delete_part_history(live,document_id,kernel,middle_bend);
        auto& deleted=live.open_part(document_id)->session;
        check(!deleted.document().find_container(middle_bend)&&
                deleted.document().history.size()==
                    converted.document.history.size()-1&&
                deleted.calculated_boundaries().back().calculation_errors.empty()&&
                deleted.calculated_boundaries().back().body_outputs.at(body)->has_solid(),
            "Deleting the middle Bend removed another sheet feature or solid");
        std::size_t retained_flats{};
        std::string invalid_flat;
        for(const auto& id:converted.created) {
            const auto* before=converted.document.find_container(id);
            if(before->feature_kind!=document::FeatureKind::Flat)continue;
            const auto* after=deleted.document().find_container(id);
            check(after,"Deleting the middle Bend removed an independent Flat");
            check(std::ranges::any_of(
                    deleted.calculated_boundaries().back().mesh.
                        triangle_references,
                    [&](const auto& face){return face.owner_id==id;}),
                ("A retained Flat is displayed under another feature identity: "+
                    id+" ("+after->name+")").c_str());
            if(deleted.document().removed_reference_states.contains(id))
                invalid_flat=id;
            ++retained_flats;
        }
        check(retained_flats==4,
            "Middle-Bend deletion did not retain all four Flats");
        check(!invalid_flat.empty()&&
                deleted.document().removed_reference_states.size()==1,
            "Middle-Bend deletion marked more than its directly attached Flat");
        auto edited=deleted.document();
        edited.find_container(invalid_flat)->name+=" edited";
        const auto edit_policy=workspace::feature_definition_calculation_policy(
            deleted.document(),deleted.calculated_boundaries(),invalid_flat);
        const auto edited_calculated=
            workspace::calculate_part_with_resolved_references(
                kernel,edited,&deleted.calculated_boundaries(),edit_policy);
        check(edited_calculated.back().calculation_errors.empty()&&
                edited_calculated.back().body_outputs.at(body)->has_solid(),
            "Opening or confirming a retained Flat Properties edit crashed or lost its solid");
        QTemporaryDir deletion_fixture("build/three-sheet-branches-XXXXXX");
        check(deletion_fixture.isValid(),
            "Cannot isolate three-branch deletion fixture");
        const auto path=std::filesystem::u8path(
            deletion_fixture.path().toStdString())/"deleted-middle-bend.prtz";
        deleted.document().save(path,deleted.calculated_boundaries());
        auto reopened=document::PartDocument::load(path);
        const auto cold=workspace::calculate_part_with_resolved_references(
            kernel,reopened);
        check(cold.back().calculation_errors.empty()&&
                cold.back().body_outputs.at(body)->has_solid(),
            "Middle-Bend deletion failed save, reopen and cold regeneration");
        check(workspace::step_part_document_history(live,document_id,false)&&
                deleted.document().find_container(middle_bend),
            "Middle-Bend deletion Undo did not restore the Bend");
        check(workspace::step_part_document_history(live,document_id,true)&&
                !deleted.document().find_container(middle_bend)&&
                deleted.calculated_boundaries().back().calculation_errors.empty(),
            "Middle-Bend deletion Redo did not retain the remaining sheets");
    }
    {
        // A vertical corner fillet crossed by the two cap fillets has the same
        // topology as the 04 native-solid fixture.  Starting on one side must
        // always grow outwards: the adjacent side is the common branch Flat
        // and remains whole, while both cap faces are split by their inferred
        // continuation Bends.  Reversing that ownership still creates eleven
        // features, so the orientation counts below protect the topology and
        // not merely the feature count.
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body(
            "Outward corner continuation");
        constexpr double height=457.040232070199;
        constexpr double radius=10;
        const std::array<std::array<double,2>,6> points{{
            {{0,0}},{{361.261461397387,0}},
            {{665.975309029948,298.8665239324305}},
            {{565.6859241492795,401.11807068178155}},
            {{302.74695795542806,143.22478672484596}},
            {{0,143.22478672484596}}}};
        auto profile=sketcher::Sketch::create_default();
        for(std::size_t index=0;index<points.size();++index) {
            const auto& first=points[index];
            const auto& second=points[(index+1)%points.size()];
            static_cast<void>(profile.add_segment(first[0],first[1],
                second[0],second[1]));
        }
        auto source=document::PartDocument::create_feature_container(
            profile.id);
        profile.owner_container_id=source.id;
        source.feature.sides[0].length=height;
        part.insert_history_entry(document::PartHistoryKind::Feature,
            source.id);
        part.history.push_back(source);
        part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(
            kernel,part);
        const auto& source_edges=
            cache.back().body_outputs.at(body)->mesh.edges;
        const auto vertical=std::ranges::find_if(
                source_edges,[&](const auto& edge) {
            return edge.points.size()>=2&&
                std::ranges::all_of(edge.points,[&](const auto point) {
                    return std::abs(point.x-points[1][0])<1e-6&&
                        std::abs(point.y-points[1][1])<1e-6;
                });
        });
        check(vertical!=source_edges.end(),
            "Outward-corner fixture lacks its vertical corner edge");
        auto corner=document::PartDocument::create_fillet_container(
            {vertical->reference});
        corner.edge_treatment.primary_size=radius;
        part.insert_history_entry(document::PartHistoryKind::Feature,
            corner.id);
        part.history.push_back(corner);
        workspace::PartCalculationPolicy policy;
        policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(
            kernel,part,&cache,policy);

        const auto& rounded_edges=
            cache.back().body_outputs.at(body)->mesh.edges;
        const auto line_distance=[](const auto point,const auto& first,
                const auto& second) {
            const kernel::Vec3 a{first[0],first[1],0};
            const kernel::Vec3 b{second[0],second[1],0};
            const kernel::Vec3 p{point.x,point.y,0};
            const auto delta=kernel::sheet_material::sub(b,a);
            return std::abs(kernel::sheet_material::cross(
                kernel::sheet_material::sub(p,a),delta).z)/
                std::sqrt(kernel::sheet_material::dot(delta,delta));
        };
        const auto cap_route=[&](double z) {
            const kernel::ViewerEdge* first_side=nullptr;
            const kernel::ViewerEdge* arc=nullptr;
            const kernel::ViewerEdge* second_side=nullptr;
            for(const auto& edge:rounded_edges) {
                if(edge.points.size()<2||!std::ranges::all_of(
                        edge.points,[&](const auto point) {
                            return std::abs(point.z-z)<1e-6;
                        }))continue;
                const bool on_first=std::ranges::all_of(
                    edge.points,[&](const auto point) {
                        return line_distance(point,points[0],points[1])<1e-6;
                    });
                const bool on_second=std::ranges::all_of(
                    edge.points,[&](const auto point) {
                        return line_distance(point,points[1],points[2])<1e-6;
                    });
                const bool in_corner=std::ranges::all_of(
                    edge.points,[&](const auto point) {
                        return std::hypot(point.x-points[1][0],
                            point.y-points[1][1])<3*radius;
                    });
                if(on_first)first_side=&edge;
                else if(on_second)second_side=&edge;
                else if(in_corner)arc=&edge;
            }
            check(first_side&&arc&&second_side,
                "Outward-corner fixture lacks a rounded cap route");
            return std::vector<kernel::EdgeReference>{
                first_side->reference,arc->reference,second_side->reference};
        };
        auto bottom_route=cap_route(0);
        auto top_route=cap_route(height);
        auto caps=document::PartDocument::create_fillet_container(
            bottom_route);
        caps.edge_treatment.routes={
            std::move(bottom_route),std::move(top_route)};
        caps.edge_treatment.route_end_faces.resize(2);
        caps.edge_treatment.primary_size=radius;
        part.insert_history_entry(document::PartHistoryKind::Feature,caps.id);
        part.history.push_back(caps);
        cache=workspace::calculate_part_with_resolved_references(
            kernel,part,&cache,policy);
        const auto& faces=
            cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[](const auto& face) {
            return face.surface&&face.surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(face.surface->origin.y)<1e-6&&
                std::abs(face.surface->axis.y)>.999999;
        });
        check(seed!=faces.end(),
            "Outward-corner fixture lacks its first side face");
        const auto converted=workspace::prepare_sheet_from_body(
            part,cache,*seed,5,kernel);
        if(converted.created.size()!=11||converted.skipped!=0)
            throw std::runtime_error(
                "Outward corner did not create six Flats and five Bends: created="+
                std::to_string(converted.created.size())+" skipped="+
                std::to_string(converted.skipped));
        std::size_t cap_flats{},side_flats{};
        std::size_t bends{};
        for(const auto& id:converted.created) {
            const auto* feature=converted.document.find_container(id);
            if(feature->feature_kind==document::FeatureKind::Bend) {
                ++bends;
                continue;
            }
            if(feature->feature_kind!=document::FeatureKind::Flat)continue;
            const auto sketch=std::ranges::find(converted.document.sketches,
                feature->flat.sketch_id,&sketcher::Sketch::id);
            check(sketch!=converted.document.sketches.end(),
                "Outward-corner Flat lost its profile");
            const auto normal=sketch->resolved_normal;
            if(std::abs(normal.z)>.999999)++cap_flats;
            else ++side_flats;
        }
        check(bends==5&&cap_flats==4&&side_flats==2,
            "Corner continuation turned back into the common branch face");
        check(converted.calculated.back().calculation_errors.empty()&&
                converted.calculated.back().body_outputs.at(body)->has_solid(),
            "Outward corner continuation produced no solid sheet");
        auto reopened=document::PartDocument::from_serialized(
            converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(
            kernel,reopened);
        check(cold.back().calculation_errors.empty()&&
                cold.back().body_outputs.at(body)->has_solid(),
            "Outward corner continuation failed cold regeneration");
    }
    {
        // A thick ordinary solid is a valid source. Its opposite-face spacing
        // is not the requested sheet thickness: the selected outer skin and
        // its analytic cylinder define Flat-Bend-Flat, while the requested
        // thickness defines the newly authored sheet.
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body("Single rounded edge");
        auto box=test::rectangular_feature(part,{100,80,20});
        part.insert_history_entry(document::PartHistoryKind::Feature,box.id);
        part.history.push_back(box);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto edge=std::ranges::find_if(
            cache.back().body_outputs.at(body)->mesh.edges,[](const auto& value) {
                return value.points.size()>=2&&
                    std::ranges::all_of(value.points,[](auto point) {
                        return std::abs(point.x+50)<1e-6&&
                            std::abs(point.y+40)<1e-6;
                    });
            });
        check(edge!=cache.back().body_outputs.at(body)->mesh.edges.end(),
            "Single-edge solid fixture lacks its rounded edge");
        auto fillet=document::PartDocument::create_fillet_container(
            {edge->reference});
        fillet.edge_treatment.primary_size=4;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);
        part.history.push_back(fillet);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(
            kernel,part,&cache,policy);
        // Use the calculated boundary packet offered to commands.  The
        // per-Body snapshot is sufficient for display, but the command packet
        // owns the complete current face adjacency used by Sheet from Body.
        const auto& mesh=cache.back().mesh;
        const auto face_key=[](const auto& face) {
            return face.owner_id+":"+face.semantic_key;
        };
        std::map<std::string,kernel::FaceReference> available;
        for(const auto& face:mesh.triangle_references)
            available.emplace(face_key(face),face);
        for(const auto& face:mesh.original_references.triangle_references)
            if(available.contains(face_key(face)))available[face_key(face)]=face;
        std::optional<kernel::FaceReference> cylinder,seed;
        for(const auto& candidate:mesh.edges) {
            if(candidate.points.empty()||
                    candidate.edge_treatment_side_references.size()!=2)continue;
            const auto first=available.find(face_key(
                candidate.edge_treatment_side_references[0]));
            const auto second=available.find(face_key(
                candidate.edge_treatment_side_references[1]));
            if(first==available.end()||second==available.end())continue;
            const auto* curved=first->second.surface&&first->second.surface->kind==
                kernel::SurfaceGeometry::Kind::Cylinder?&first->second:
                second->second.surface&&second->second.surface->kind==
                kernel::SurfaceGeometry::Kind::Cylinder?&second->second:nullptr;
            const auto* plane=first->second.surface&&first->second.surface->kind==
                kernel::SurfaceGeometry::Kind::Plane?&first->second:
                second->second.surface&&second->second.surface->kind==
                kernel::SurfaceGeometry::Kind::Plane?&second->second:nullptr;
            if(!curved||!plane)
                continue;
            using namespace kernel::sheet_material;
            auto plane_normal=plane->surface->axis;
            if(plane->surface->reversed)plane_normal=mul(plane_normal,-1);
            const auto at=candidate.points[candidate.points.size()/2];
            const auto radial=sub(at,curved->surface->origin);
            auto cylinder_normal=unit(sub(radial,mul(curved->surface->axis,
                dot(radial,curved->surface->axis))));
            if(curved->surface->reversed)
                cylinder_normal=mul(cylinder_normal,-1);
            if(dot(plane_normal,cylinder_normal)<.999999)continue;
            cylinder=*curved;seed=*plane;break;
        }
        check(cylinder&&seed,
            "Single-edge solid fixture lacks an adjacent planar skin");
        const auto converted=workspace::prepare_sheet_from_body(
            part,cache,*seed,1,kernel);
        check(converted.created.size()==3&&converted.skipped==0,
            "Thick solid skin did not convert to Flat-Bend-Flat");
        const auto bend=std::ranges::find_if(converted.created,[&](const auto& id) {
            return converted.document.find_container(id)->feature_kind==
                document::FeatureKind::Bend;
        });
        check(bend!=converted.created.end(),
            "Thick solid skin conversion created no Bend");
        const auto following_flat=std::next(bend);
        check(following_flat!=converted.created.end()&&
                converted.document.find_container(*following_flat)->feature_kind==
                    document::FeatureKind::Flat,
            "Thick solid skin conversion has no Flat after its Bend");
        const auto following_sketch_id=converted.document.find_container(
            *following_flat)->flat.sketch_id;
        const auto& following_sketch=*std::ranges::find(
            converted.document.sketches,following_sketch_id,&sketcher::Sketch::id);
        std::map<std::string,std::string> endpoint_points;
        for(const auto& constraint:following_sketch.constraints)
            if(constraint.kind==sketcher::ConstraintKind::PointReference&&
                    (constraint.second_point_id==following_sketch.id+
                        ":flat-attachment:first"||
                     constraint.second_point_id==following_sketch.id+
                        ":flat-attachment:last"))
                endpoint_points.emplace(constraint.second_point_id,
                    constraint.first_point_id);
        check(endpoint_points.size()==2,
            "Converted Flat outline is not bound to both Bend End vertices");
        const auto joining_segment=std::ranges::find_if(
            following_sketch.segments,[&](const auto& segment) {
                const auto& first_point=endpoint_points.at(
                    following_sketch.id+":flat-attachment:first");
                const auto& last_point=endpoint_points.at(
                    following_sketch.id+":flat-attachment:last");
                return (segment.first_point_id==first_point&&
                        segment.second_point_id==last_point)||
                    (segment.first_point_id==last_point&&
                        segment.second_point_id==first_point);
            });
        check(joining_segment!=following_sketch.segments.end(),
            "Converted Flat endpoints do not move one complete joining edge");
        const auto initial_first=following_sketch.find_point(endpoint_points.at(
            following_sketch.id+":flat-attachment:first"));
        const auto initial_last=following_sketch.find_point(endpoint_points.at(
            following_sketch.id+":flat-attachment:last"));
        const auto first_reference=std::ranges::find(
            following_sketch.external_references,
            following_sketch.id+":flat-attachment:first",
            &sketcher::SketchExternalReference::id);
        const auto last_reference=std::ranges::find(
            following_sketch.external_references,
            following_sketch.id+":flat-attachment:last",
            &sketcher::SketchExternalReference::id);
        check(initial_first&&initial_last&&
                first_reference!=following_sketch.external_references.end()&&
                last_reference!=following_sketch.external_references.end()&&
                first_reference->cached_points.size()==1&&
                last_reference->cached_points.size()==1&&
                std::hypot(initial_first->x-
                        first_reference->cached_points.front()[0],
                    initial_first->y-
                        first_reference->cached_points.front()[1])<1e-7&&
                std::hypot(initial_last->x-
                        last_reference->cached_points.front()[0],
                    initial_last->y-
                        last_reference->cached_points.front()[1])<1e-7&&
                std::abs(initial_last->x-initial_first->x)>1e-6,
            "Converted Flat joining edge does not follow both Bend End vertices");
        const double initial_endpoint_direction=initial_last->x-initial_first->x;
        near(converted.document.find_container(*bend)->bend.radius,
            cylinder->surface->radius-(cylinder->surface->reversed?0:1),.001);
        check(converted.calculated.back().calculation_errors.empty()&&
                converted.calculated.back().body_outputs.at(body)->has_solid(),
            "Thick solid skin conversion produced no solid sheet");
        for(const auto corners:{std::array{true,false},std::array{true,true},
                std::array{false,true},std::array{false,false}}) {
            auto cornered=converted.document;
            auto* corner_bend=cornered.find_container(*bend);
            corner_bend->bend.corner=corners;corner_bend->bend.corner_gap=.05;
            const auto cornered_cache=workspace::calculate_part_with_resolved_references(
                kernel,cornered,&converted.calculated,policy);
            const auto cornered_result=cornered_cache.back().body_outputs.at(body).get();
            check(cornered_result.calculation_errors.empty()&&
                    cornered.find_container(*following_flat)->placement.reference_valid,
                "Converted Bend corner closure broke its following Flat");
            const auto shape=kernel::read_calculated_body_shape(cornered_result);
            unsigned solids=0;
            for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
            check(solids==1,
                "Converted Bend corner closure split its following Flat");
            const auto& cornered_sketch=*std::ranges::find(cornered.sketches,
                following_sketch_id,&sketcher::Sketch::id);
            const auto* first_point=cornered_sketch.find_point(endpoint_points.at(
                following_sketch.id+":flat-attachment:first"));
            const auto* last_point=cornered_sketch.find_point(endpoint_points.at(
                following_sketch.id+":flat-attachment:last"));
            check(first_point&&last_point&&
                    (last_point->x-first_point->x)*initial_endpoint_direction>0,
                "Converted Bend corner closure reversed Flat endpoint orientation");
        }
        document::DocumentSession session(
            converted.document,converted.calculated);
        check(!session.body_context_mesh().triangles.empty(),
            "Sheet from Body boundary hid the converted Body context");
        auto reopened=document::PartDocument::from_serialized(
            converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(
            kernel,reopened);
        check(cold.back().calculation_errors.empty()&&
                cold.back().body_outputs.at(body)->has_solid(),
            "Thick solid skin conversion failed cold regeneration");
        workspace::Workspace live;
        const auto document_id = reopened.document_id;
        live.add_part(std::move(reopened), cold);
        workspace::delete_part_history(live, document_id, kernel, *bend);
        const auto& deleted = live.open_part(document_id)->session;
        check(deleted.document().find_container(converted.container_id) &&
                deleted.document().find_container(converted.created.front()) &&
                !deleted.document().find_container(*bend) &&
                deleted.document().find_container(*following_flat) &&
                deleted.document().removed_reference_states.contains(*following_flat),
            "Deleting a generated Bend did not retain its dependent Flat as invalid");
        QTemporaryDir deletion_fixture("build/sheet-divider-XXXXXX");
        check(deletion_fixture.isValid(), "Cannot isolate Sheet from Body deletion fixture");
        const auto deleted_path = std::filesystem::u8path(deletion_fixture.path().toStdString()) / "deleted-bend.prtz";
        deleted.document().save(deleted_path, deleted.calculated_boundaries());
        const auto loaded = document::PartDocument::load(deleted_path);
        check(!loaded.find_container(*bend) && loaded.find_container(*following_flat) &&
                loaded.removed_reference_states.contains(*following_flat),
            "Native reopening lost the invalid dependent Flat");
        check(workspace::step_part_document_history(live, document_id, false) &&
                deleted.document().find_container(*bend) &&
                !deleted.document().removed_reference_states.contains(*following_flat),
            "Generated Bend deletion Undo did not restore its dependent Flat");
        check(workspace::step_part_document_history(live, document_id, true) &&
                !deleted.document().find_container(*bend) &&
                deleted.document().removed_reference_states.contains(*following_flat),
            "Generated Bend deletion Redo lost the invalid dependent Flat");
        check(workspace::step_part_document_history(live, document_id, false),
            "Cannot restore Bend before deleting its source Flat");
        const auto original_bend = *deleted.document().find_container(*bend);
        const auto original_section = *std::ranges::find(deleted.document().sketches,
            original_bend.bend.sketch_id, &sketcher::Sketch::id);
        const auto check_retained_bend = [&](const auto& document) {
            const auto* feature = document.find_container(*bend);
            check(feature && !feature->placement.reference_valid &&
                    document.removed_reference_states.contains(*bend),
                "Deleting the source Flat did not retain an invalid Bend");
            auto expected = original_bend.placement;
            expected.references.clear();
            expected.reference_valid = false;
            check(feature->placement == expected &&
                    feature->bend.auxiliary_sketches == original_bend.bend.auxiliary_sketches,
                "The invalid Bend lost its last frame or auxiliary Sketches");
            const auto section = std::ranges::find(document.sketches,
                feature->bend.sketch_id, &sketcher::Sketch::id);
            check(section != document.sketches.end() &&
                    section->resolved_origin == original_section.resolved_origin &&
                    section->resolved_x_axis == original_section.resolved_x_axis &&
                    section->resolved_y_axis == original_section.resolved_y_axis &&
                    section->resolved_normal == original_section.resolved_normal &&
                    section->dimensions == original_section.dimensions,
                "The invalid Bend lost its start frame or endpoint dimensions");
        };
        workspace::delete_part_history(live, document_id, kernel, converted.created.front());
        check_retained_bend(deleted.document());
        const auto bend_path = deleted_path.parent_path() / "missing-bend-source.prtz";
        deleted.document().save(bend_path, deleted.calculated_boundaries());
        auto broken_bend = document::PartDocument::load(bend_path);
        check_retained_bend(broken_bend);
        static_cast<void>(workspace::calculate_part_with_resolved_references(kernel, broken_bend));
        check_retained_bend(broken_bend);
        check(workspace::step_part_document_history(live, document_id, false) &&
                deleted.document().find_container(*bend)->placement == original_bend.placement,
            "Undo did not restore the Bend's valid placement");
        check(workspace::step_part_document_history(live, document_id, true),
            "Source Flat deletion Redo failed");
        check_retained_bend(deleted.document());
    }
    if (include_oblique_corners) {
        // Every rounded edge of a polygonal base is a separate Bend branch.
        // Where two source cylinders meet, their native bends must close with
        // zero gap. The closure calculates the thickness-dependent oblique
        // inner intersection; simply sweeping the full outer edges overlaps.
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body("Oblique Bend corners");
        auto profile=sketcher::Sketch::create_default();
        constexpr double radius=50;
        std::array<std::array<double,2>,6> points{};
        for(std::size_t i=0;i<points.size();++i) {
            const double angle=2*std::numbers::pi*i/points.size();
            points[i]={radius*std::cos(angle),radius*std::sin(angle)};
        }
        for(std::size_t i=0;i<points.size();++i) {
            const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
            static_cast<void>(profile.add_segment(a[0],a[1],b[0],b[1]));
        }
        auto source=document::PartDocument::create_feature_container(profile.id);
        profile.owner_container_id=source.id;
        source.feature.sides[0].length=40;
        source.feature.sides[0].draft_angle_degrees=20;
        source.feature.sides[1].operation=
            document::FeatureSideOperation::Extrusion;
        source.feature.sides[1].length=40;
        source.feature.sides[1].draft_angle_degrees=20;
        part.insert_history_entry(document::PartHistoryKind::Feature,source.id);
        part.history.push_back(source);part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        double bottom=INFINITY;
        for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
            for(const auto point:edge.points)bottom=std::min(bottom,point.z);
        std::vector<kernel::EdgeReference> bottom_edges;
        for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
            if(edge.points.size()>=2&&std::ranges::all_of(edge.points,
                    [&](const auto& point){return std::abs(point.z-bottom)<1e-6;}))
                bottom_edges.push_back(edge.reference);
        check(bottom_edges.size()==6,
            "Oblique corner fixture lacks its six base edges");
        auto fillet=document::PartDocument::create_fillet_container(bottom_edges);
        fillet.edge_treatment.routes.clear();
        fillet.edge_treatment.route_end_faces.clear();
        for(const auto& edge:bottom_edges) {
            fillet.edge_treatment.routes.push_back({edge});
            fillet.edge_treatment.route_end_faces.emplace_back();
        }
        fillet.edge_treatment.primary_size=5;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);
        part.history.push_back(fillet);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(
            kernel,part,&cache,policy);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[&](const auto& face) {
            return face.surface&&face.surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(face.surface->origin.z-bottom)<1e-6&&
                std::abs(face.surface->axis.z)>.999999;
        });
        check(seed!=faces.end(),
            "Oblique corner fixture lacks its rounded base face");
        const auto sheet_body=part.body_history.create_body(
            "Converted oblique sheet");
        const auto converted=workspace::prepare_sheet_from_body(
            part,cache,*seed,2,kernel);
        check(converted.created.size()==13&&converted.skipped==0,
            "Oblique polygon skin did not create six Bend branches");
        std::size_t bends=0;
        struct BendEndCurve {
            std::string owner;
            std::string role;
            std::vector<kernel::Vec3> points;
        };
        std::vector<BendEndCurve> end_curves;
        for(const auto& id:converted.created) {
            const auto* feature=converted.document.find_container(id);
            if(feature->feature_kind!=document::FeatureKind::Bend)continue;
            ++bends;
            check(feature->bend.corner==std::array{true,true}&&
                    feature->bend.corner_gap==0,
                "Converted oblique Bend did not retain zero-gap corner closure");
            const auto sketch=std::ranges::find(
                converted.document.sketches,feature->bend.sketch_id,
                &sketcher::Sketch::id);
            check(sketch!=converted.document.sketches.end(),
                "Converted oblique Bend lost its start Sketch");
            const auto preview=document::bend_preview(*feature,*sketch,
                document::sheet_metal_defaults(converted.document));
            if(std::getenv("ZIMA_CPP_SHEET_CORNER_PROFILE")) {
                const auto extensions=
                    document::bend_profile_extensions(*feature);
                std::cout<<"oblique_bend="<<id<<" extensions="
                    <<extensions[0]<<','<<extensions[1]<<'\n';
            }
            for(const auto& edge:preview.edges)
                for(const auto* role:{"outer-first","outer-last",
                        "inner-first","inner-last"})
                    if(edge.reference.semantic_key.find(role)!=
                            std::string::npos)
                        end_curves.push_back({id,role,edge.points});
        }
        const auto point_distance=[](const auto& a,const auto& b) {
            return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
        };
        const auto curve_gap=[&](const auto& a,const auto& b) {
            double result{};
            for(const auto* first:{&a,&b}) {
                const auto* second=first==&a?&b:&a;
                for(const auto& point:*first) {
                    double nearest=INFINITY;
                    for(const auto& other:*second)
                        nearest=std::min(nearest,
                            point_distance(point,other));
                    result=std::max(result,nearest);
                }
            }
            return result;
        };
        for(const auto& curve:end_curves) {
            double nearest=INFINITY;
            for(const auto& other:end_curves)
                if(other.owner!=curve.owner&&
                        other.role.starts_with(
                            curve.role.starts_with("outer")?
                                "outer":"inner"))
                    nearest=std::min(nearest,
                        curve_gap(curve.points,other.points));
            if(curve.role.starts_with("inner"))
                check(nearest<1e-7,
                    "Measured oblique Bend inner endpoints do not close");
            if(std::getenv("ZIMA_CPP_SHEET_CORNER_PROFILE"))
                std::cout<<"oblique_end="<<curve.owner<<':'<<curve.role
                    <<" nearest_gap="<<nearest<<'\n';
        }
        std::size_t attached_flats{},endpoint_bindings{};
        for(const auto& id:converted.created) {
            const auto* feature=converted.document.find_container(id);
            if(!feature||feature->feature_kind!=document::FeatureKind::Flat||
                    !feature->flat.sheet_attachment)continue;
            ++attached_flats;
            const auto sketch=std::ranges::find(converted.document.sketches,
                feature->flat.sketch_id,&sketcher::Sketch::id);
            check(sketch!=converted.document.sketches.end(),
                "Converted oblique Flat lost its profile Sketch");
            check(std::ranges::none_of(sketch->segments,
                    &sketcher::SketchSegment::construction),
                "Converted Flat retained a visible construction guide");
            endpoint_bindings+=std::ranges::count_if(sketch->constraints,
                [](const auto& constraint) {
                    return !constraint.suppressed&&constraint.kind==
                        sketcher::ConstraintKind::PointReference;
                });
        }
        check(attached_flats==6&&endpoint_bindings==12,
            "Oblique conversion did not bind both Flat endpoints directly");
        check(bends==6&&
                converted.calculated.back().calculation_errors.empty()&&
                converted.calculated.back().body_outputs.at(sheet_body)->has_solid(),
            "Oblique zero-gap Bend corners did not produce one sheet solid");
        auto reopened=document::PartDocument::from_serialized(
            converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(
            kernel,reopened);
        check(cold.back().calculation_errors.empty()&&
                cold.back().body_outputs.at(sheet_body)->has_solid(),
            "Oblique zero-gap Bend corners failed cold regeneration");
        workspace::Workspace deletion_live;
        const auto deletion_document=converted.document.document_id;
        deletion_live.add_part(converted.document,converted.calculated);
        auto deletion_ids=converted.created;
        std::reverse(deletion_ids.begin(),deletion_ids.end());
        if(!converted.container_id.empty())
            deletion_ids.push_back(converted.container_id);
        auto& deletion_session=deletion_live.open_part(
            deletion_document)->session;
        const auto deletion_revision=deletion_session.revision();
        const auto deletion_started=std::chrono::steady_clock::now();
        workspace::delete_part_history(deletion_live,deletion_document,kernel,
            deletion_ids);
        const auto deletion_ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-deletion_started).count();
        std::cout<<"Batch sheet-history deletion: "<<deletion_ms<<" ms\n";
        check(deletion_session.revision()==deletion_revision+1&&
                std::ranges::none_of(deletion_ids,[&](const auto& id) {
                    return deletion_session.document().find_container(id)!=nullptr;
                })&&deletion_session.calculated_boundaries().back().
                    calculation_errors.empty()&&
                deletion_session.calculated_boundaries().back().
                    body_outputs.at(body)->has_solid(),
            "Batch deletion did not remove the selected sheet set in one transaction");
        check(deletion_ms<=1000.,
            "Batch deletion of the representative sheet set exceeded one second");
        check(workspace::step_part_document_history(deletion_live,
                    deletion_document,false)&&
                std::ranges::all_of(deletion_ids,[&](const auto& id) {
                    return deletion_session.document().find_container(id)!=nullptr;
                }),
            "Batch sheet deletion Undo did not restore the complete selected set");
        check(workspace::step_part_document_history(deletion_live,
                    deletion_document,true)&&
                std::ranges::none_of(deletion_ids,[&](const auto& id) {
                    return deletion_session.document().find_container(id)!=nullptr;
                }),
            "Batch sheet deletion Redo did not remove the complete selected set");
    }
    {
        // A source profile may carry one real bend radius along its selected
        // skin while its opposite skin remains a sharp two-branch corner.
        // Reconstruct both Flats from that common analytic split and ignore
        // unrelated boundary roundovers instead of following them as Bends.
        auto part=document::PartDocument::create_default();const auto body=part.body_history.create_body("Branched solid");
        auto profile=sketcher::Sketch::create_default();
        constexpr double thickness=20;
        const double offset=thickness/std::sqrt(2.);
        const double split=120-thickness*std::sqrt(2.);
        const std::array<std::array<double,2>,6> points{{
            {{0,0}},{{100,0}},{{160,60}},{{160-offset,60+offset}},{{split,20}},{{0,20}}}};
        for(std::size_t i=0;i<points.size();++i) {
            const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
            static_cast<void>(profile.add_segment(a[0],a[1],b[0],b[1]));
        }
        auto source=document::PartDocument::create_extrusion_container(profile.id);
        profile.owner_container_id=source.id;source.extrusion.length_forward=100;
        part.insert_history_entry(document::PartHistoryKind::Feature,source.id);
        part.history.push_back(source);part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& source_edges=cache.back().body_outputs.at(body)->mesh.edges;
        const auto bend_edge=std::ranges::find_if(source_edges,[](const auto& edge) {
            if(edge.points.size()<2)return false;
            double low=INFINITY,high=-INFINITY;
            for(const auto point:edge.points) {
                if(std::abs(point.x-100)>1e-5||std::abs(point.y)>1e-5)return false;
                low=std::min(low,point.z);high=std::max(high,point.z);
            }
            return high-low>99;
        });
        check(bend_edge!=source_edges.end(),"Branched solid fixture lacks its bend edge");
        auto fillet=document::PartDocument::create_fillet_container({bend_edge->reference});
        fillet.edge_treatment.primary_size=10;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);part.history.push_back(fillet);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache,policy);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(face.surface->origin.y)<1e-5&&std::abs(face.surface->axis.y)>.999;
        });
        check(seed!=faces.end(),"Branched solid fixture lacks its first skin face");
        const auto before=part.serialized();
        // The source's outer cylinder radius equals the requested material
        // thickness. The native result must preserve the legitimate zero
        // inside radius instead of dropping the Bend and leaving a gap.
        auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,10,kernel);
        check(part.serialized()==before,"Branched source was modified during conversion");
        check(converted.created.size()==3&&converted.skipped==0,
            "Sharp opposite corner did not split into Flat-Bend-Flat");
        check(converted.document.find_container(converted.created[0])->feature_kind==document::FeatureKind::Flat&&
            converted.document.find_container(converted.created[1])->feature_kind==document::FeatureKind::Bend&&
            converted.document.find_container(converted.created[2])->feature_kind==document::FeatureKind::Flat,
            "Branched sheet history is not Flat-Bend-Flat");
        near(converted.document.find_container(converted.created[1])->bend.radius,0,.001);
        const auto output=converted.calculated.back().body_outputs.at(body);
        check(output->calculation_errors.empty()&&output->has_solid(),
            "Branched sheet has a gap between its two Flats");
        auto reopened=document::PartDocument::from_serialized(converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened);
        check(cold.back().calculation_errors.empty()&&cold.back().body_outputs.at(body)->has_solid(),
            "Branched sheet gap appeared after cold regeneration");
        const auto bend_face=std::ranges::find_if(faces,[](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                std::abs(face.surface->radius-10)<1e-5&&std::abs(face.surface->axis.z)>.999;
        });
        check(bend_face!=faces.end(),"Branched solid fixture lacks its cylindrical bend face");
        const auto reverse_seed=std::ranges::find_if(faces,[&](const auto& face) {
            if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||face==*seed)return false;
            return std::ranges::any_of(cache.back().mesh.edges,[&](const auto& edge) {
                return std::ranges::find(edge.edge_treatment_side_references,*bend_face)!=edge.edge_treatment_side_references.end()&&
                    std::ranges::find(edge.edge_treatment_side_references,face)!=edge.edge_treatment_side_references.end();
            });
        });
        check(reverse_seed!=faces.end(),"Branched solid fixture lacks its second skin face");
        const auto reverse=workspace::prepare_sheet_from_body(part,cache,*reverse_seed,10,kernel);
        check(reverse.created.size()==3&&reverse.skipped==0&&
            reverse.calculated.back().body_outputs.at(body)->calculation_errors.empty(),
            "Reverse branch selection did not produce the same gap-free sheet history");
    }
    {
        // A rounded rectangular tube is a closed alternating plane/cylinder
        // skin. Conversion must terminate by leaving one Bend out as a seam;
        // a native sheet history may neither cycle nor end with a Bend.
        auto part=document::PartDocument::create_default();const auto body=part.body_history.create_body("Closed skin");
        auto profile=sketcher::Sketch::create_default();
        const auto rounded_loop=[&](double x,double y,double radius) {
            const double cx=x-radius,cy=y-radius;
            static_cast<void>(profile.add_segment(-cx,-y,cx,-y));
            static_cast<void>(profile.add_arc(cx,-cy,cx,-y,x,-cy));
            static_cast<void>(profile.add_segment(x,-cy,x,cy));
            static_cast<void>(profile.add_arc(cx,cy,x,cy,cx,y));
            static_cast<void>(profile.add_segment(cx,y,-cx,y));
            static_cast<void>(profile.add_arc(-cx,cy,-cx,y,-x,cy));
            static_cast<void>(profile.add_segment(-x,cy,-x,-cy));
            static_cast<void>(profile.add_arc(-cx,-cy,-x,-cy,-cx,-y));
        };
        rounded_loop(50,40,10);rounded_loop(48,38,8);
        auto feature=document::PartDocument::create_extrusion_container(profile.id);
        profile.owner_container_id=feature.id;feature.extrusion.length_forward=20;
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
        part.history.push_back(feature);part.sketches.push_back(profile);
        const auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(face.surface->axis.z)<1e-7&&face.surface->origin.x>49;
        });
        check(seed!=faces.end(),"Closed sheet fixture has no outer planar seed");
        const auto started=std::chrono::steady_clock::now();
        auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,2,kernel);
        check(std::chrono::steady_clock::now()-started<std::chrono::seconds(10),
            "Closed sheet traversal did not terminate promptly");
        int flats=0,bends=0;
        for(const auto& owner:converted.created) {
            const auto* item=converted.document.find_container(owner);
            flats+=item->feature_kind==document::FeatureKind::Flat;
            bends+=item->feature_kind==document::FeatureKind::Bend;
        }
        check(converted.skipped==0&&flats==bends+1&&flats>=2,
            "Closed sheet did not retain exactly one finite seam");
        check(converted.document.find_container(converted.created.back())->feature_kind==document::FeatureKind::Flat,
            "Closed sheet conversion ended with a Bend");
        auto reopened=document::PartDocument::from_serialized(converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened);
        check(cold.back().calculation_errors.empty(),"Closed sheet seam failed cold regeneration");
        static_cast<void>(body);
    }
    for(const auto direction:{document::ExtrusionDirection::Forward,
            document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        const bool placed=direction!=document::ExtrusionDirection::Forward;
        auto part=document::PartDocument::create_default();const auto source=part.body_history.create_body("Source");
        auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=feature.id;feature.flat.sketch_id=profile.id;
        feature.flat.thickness_override=true;feature.flat.thickness=3;feature.flat.direction=direction;
        static_cast<void>(profile.add_segment(0,0,100,0));static_cast<void>(profile.add_segment(100,0,100,80));
        static_cast<void>(profile.add_segment(100,80,0,80));static_cast<void>(profile.add_segment(0,80,0,0));
        for(double y:{20.,50.}) {
            static_cast<void>(profile.add_segment(20,y-5,40,y-5));
            static_cast<void>(profile.add_arc(40,y,40,y-5,40,y+5));
            static_cast<void>(profile.add_segment(40,y+5,20,y+5));
            static_cast<void>(profile.add_arc(20,y,20,y+5,20,y-5));
        }
        static_cast<void>(profile.add_circle(70,40,6));
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);part.sketches.push_back(profile);
        if(placed) {
            auto body=*part.body_history.find(source);body.scope.placement.x=17;body.scope.placement.y=-23;
            body.scope.placement.rotation_x=body.scope.placement.absolute_rotation_x=27;
            body.scope.placement.rotation_y=body.scope.placement.absolute_rotation_y=-19;
            body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=41;part.body_history.update_body(body);
        }
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const double expected=(8000-2*(200+25*std::numbers::pi)-36*std::numbers::pi)*3;
        near(measured_volume(kernel,cache.back().body_outputs.at(source)),expected,.001);
        if(direction==document::ExtrusionDirection::Forward) {
            auto flat_body=cache.back().body_outputs.at(source).get();
            flat_body.mesh.axes.push_back({{50,20,0},{1,0,0},100,{"fixture","sheet-bend-line:from:slot",{}},""});
            const auto clipped=kernel::sheet_flat_contour(flat_body,3,.05);
            check(clipped.bend_axes.size()==2,"Bend axis did not split at the stadium slot");
            double length=0;for(const auto& axis:clipped.bend_axes)length+=std::hypot(axis[1].x-axis[0].x,axis[1].y-axis[0].y);
            near(length,70,1e-5); // 100 mm width minus the 30 mm slot at its centre.
        }
        check(document::profile_status(part.sketches.front())==document::ProfileStatus::Closed,"Slot profile is not editable as a closed profile");
        check(!document::flat_preview(part.history.front(),part.sketches.front(),document::sheet_metal_defaults(part)).edges.empty(),"Multi-slot Flat lacks an edit preview");
        const auto stable_seed=std::ranges::find_if(cache.back().mesh.original_references.triangle_references,[](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                face.sheet_role==kernel::SheetFaceRole::SideA;
        });
        check(stable_seed!=cache.back().mesh.original_references.triangle_references.end(),"Flat fixture has no stable Side A seed");
        const auto seed=*stable_seed;
        near(workspace::suggest_sheet_thickness(cache.back().mesh,seed),3);
        if(direction==document::ExtrusionDirection::Forward) {
            // OCCT's native STEP writer cannot access the packaged-process
            // redirection used by QTemporaryDir's system default on Windows.
            // Keep this disposable fixture under the repository build tree.
            QTemporaryDir directory(QStringLiteral("build/sheet-exchange-XXXXXX"));check(directory.isValid(),"Cannot create STEP fixture directory");
            const auto path=std::filesystem::u8path(directory.path().toStdString())/"source.step";
            kernel.export_step(std::vector<kernel::PlacedBody>{{cache.back().body_outputs.at(source).get(),{},{}}},path.string());
            auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},path);
            const auto& references=imported.calculated.back().mesh.original_references.triangle_references;
            const auto face=std::ranges::max_element(references,{},[](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Plane?f.measured_area.value_or(0):0;});
            check(face!=references.end(),"Imported STEP lacks a planar seed");
            auto converted=workspace::prepare_sheet_from_body(imported.document,imported.calculated,*face,2,kernel);
            const auto body=converted.document.body_history.active_body_id();
            near(measured_volume(kernel,converted.calculated.back().body_outputs.at(body)),expected*2/3,.001);
            converted.document.erase_history_object(imported.document.history.front().id);
            auto reopened=document::PartDocument::from_serialized(converted.document.serialized());
            const auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened);
            near(measured_volume(kernel,cold.back().body_outputs.at(body)),expected*2/3,.001);
        }
        {
            const auto original=part.serialized();
            auto same=workspace::prepare_sheet_from_body(part,cache,seed,2,kernel);
            check(part.serialized()==original,"Same-Body conversion changed its input");
            const auto* boundary=same.document.find_container(same.container_id);
            check(boundary&&boundary->feature_kind==
                    document::FeatureKind::SheetFromBody&&
                    boundary->sheet_from_body.generated_features==same.created&&
                    boundary->sheet_from_body.child_bindings.size()==
                        same.created.size()&&
                    boundary->sheet_from_body.child_bindings.front().root&&
                    boundary->sheet_from_body.child_bindings.front().
                        feature_id==same.created.front()&&
                    boundary->sheet_from_body.child_bindings.front().
                        faces.size()>=2&&
                    boundary->sheet_from_body.replaces_body&&
                    boundary->sheet_from_body.source_body_id==source,
                "Same-Body conversion did not persist its history boundary");
            const auto operations = same.document.kernel_operations();
            const auto divider = std::ranges::find(operations, same.container_id,
                &kernel::HistoryOperation::owner_id);
            check(same.created.size() == 1 && divider != operations.end() &&
                    divider->replaces_body && std::next(divider) != operations.end() &&
                    !std::next(divider)->replaces_body,
                "The divider must replace material independently of the first Flat");
            near(measured_volume(kernel,same.calculated.back().body_outputs.at(source)),expected*2/3,.001);
            same.document.history.front().flat.thickness=7;
            same.calculated=workspace::calculate_part_with_resolved_references(kernel,same.document);
            near(measured_volume(kernel,same.calculated.back().body_outputs.at(source)),expected*2/3,.001);
            auto reopened=document::PartDocument::from_serialized(same.document.serialized());
            const auto* current_boundary=same.document.find_container(
                same.container_id);
            const auto* reopened_boundary=reopened.find_container(
                same.container_id);
            check(current_boundary&&reopened_boundary&&
                    reopened_boundary->sheet_from_body.child_bindings==
                        current_boundary->sheet_from_body.child_bindings,
                "Native round trip changed Sheet from Body source bindings");
            auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened);
            near(measured_volume(kernel,cold.back().body_outputs.at(source)),expected*2/3,.001);
            if(direction==document::ExtrusionDirection::Forward) {
                auto linked=same;
                const auto generated_ids=linked.created;
                std::vector<std::string> generated_sketch_ids;
                for(const auto& id:generated_ids) {
                    const auto* child=linked.document.find_container(id);
                    generated_sketch_ids.push_back(
                        child->feature_kind==document::FeatureKind::Flat?
                            child->flat.sketch_id:child->bend.sketch_id);
                }
                const auto source_profile=std::ranges::find(
                    linked.document.sketches,feature.id,
                    &sketcher::Sketch::owner_container_id);
                check(source_profile!=linked.document.sketches.end(),
                    "Associative source Flat lost its owned Sketch");
                for(auto& point:source_profile->points)
                    if(std::abs(point.x-100)<1e-9)point.x=120;
                linked.calculated=
                    workspace::calculate_part_with_resolved_references(
                        kernel,linked.document,&linked.calculated);
                const double first_edit_volume=expected*2/3+20*80*2;
                near(measured_volume(kernel,linked.calculated.back().
                    body_outputs.at(source)),first_edit_volume,.001);
                const auto* linked_parent=linked.document.find_container(
                    linked.container_id);
                check(linked_parent&&
                        linked_parent->sheet_from_body.generated_features==
                            generated_ids&&
                        std::ranges::all_of(linked_parent->sheet_from_body.
                            child_bindings,[](const auto& binding) {
                                return binding.references_valid;
                            }),
                    "Source dimension edit lost Sheet from Body identities or bindings");
                for(std::size_t index=0;index<generated_ids.size();++index) {
                    const auto* child=linked.document.find_container(
                        generated_ids[index]);
                    check(child&&
                            (child->feature_kind==document::FeatureKind::Flat?
                                child->flat.sketch_id:child->bend.sketch_id)==
                                    generated_sketch_ids[index],
                        "Source dimension edit changed a generated Sketch identity");
                }

                auto second_edit=linked.document;
                const auto second_profile=std::ranges::find(
                    second_edit.sketches,feature.id,
                    &sketcher::Sketch::owner_container_id);
                for(auto& point:second_profile->points)
                    if(std::abs(point.x-120)<1e-9)point.x=135;
                auto second_calculated=
                    workspace::calculate_part_with_resolved_references(
                        kernel,second_edit,&linked.calculated);
                const double second_edit_volume=expected*2/3+35*80*2;
                near(measured_volume(kernel,second_calculated.back().
                    body_outputs.at(source)),second_edit_volume,.001);
                const auto settled_definition=second_edit.serialized();
                const auto settled_fingerprint=second_calculated.back().
                    source_fingerprint;
                auto repeated=
                    workspace::calculate_part_with_resolved_references(
                        kernel,second_edit,&second_calculated);
                check(second_edit.serialized()==settled_definition&&
                        repeated.back().source_fingerprint==
                            settled_fingerprint,
                    "Unchanged associative regeneration rewrote the document");

                workspace::Workspace association_live;
                const auto association_id=same.document.document_id;
                association_live.add_part(same.document,same.calculated);
                workspace::commit_part_document(association_live,
                    association_id,linked.document,linked.calculated);
                workspace::commit_part_document(association_live,
                    association_id,second_edit,second_calculated);
                auto& association_session=association_live.open_part(
                    association_id)->session;
                check(workspace::step_part_document_history(
                            association_live,association_id,false),
                    "First associative source edit Undo failed");
                near(measured_volume(kernel,association_session.
                    calculated_boundaries().back().body_outputs.at(source)),
                    first_edit_volume,.001);
                check(workspace::step_part_document_history(
                            association_live,association_id,false),
                    "Second associative source edit Undo failed");
                near(measured_volume(kernel,association_session.
                    calculated_boundaries().back().body_outputs.at(source)),
                    expected*2/3,.001);
                check(workspace::step_part_document_history(
                            association_live,association_id,true)&&
                        workspace::step_part_document_history(
                            association_live,association_id,true),
                    "Associative source edit Redo failed");
                near(measured_volume(kernel,association_session.
                    calculated_boundaries().back().body_outputs.at(source)),
                    second_edit_volume,.001);

                auto linked_reopened=document::PartDocument::from_serialized(
                    second_edit.serialized(second_calculated));
                const auto linked_cold=
                    workspace::calculate_part_with_resolved_references(
                        kernel,linked_reopened);
                near(measured_volume(kernel,linked_cold.back().
                    body_outputs.at(source)),second_edit_volume,.001);
                const auto* cold_parent=linked_reopened.find_container(
                    linked.container_id);
                check(cold_parent&&cold_parent->sheet_from_body.
                        generated_features==generated_ids,
                    "Cold reopen changed associative child identities");

                auto broken=second_edit;
                broken.erase_history_object(feature.id);
                const auto broken_calculated=
                    workspace::calculate_part_with_resolved_references(
                        kernel,broken,&second_calculated);
                near(measured_volume(kernel,broken_calculated.back().
                    body_outputs.at(source)),second_edit_volume,.001);
                const auto* broken_parent=broken.find_container(
                    linked.container_id);
                check(broken_parent&&std::ranges::all_of(
                        broken_parent->sheet_from_body.child_bindings,
                        [](const auto& binding) {
                            return !binding.references_valid;
                        }),
                    "Missing source did not retain and mark last-valid generated sheets");
            }
            workspace::Workspace live;const auto document_id=reopened.document_id;
            live.add_part(std::move(reopened),std::move(cold));
            auto* session=&live.open_part(document_id)->session;
            workspace::delete_part_history(live, document_id, kernel, same.created.front());
            check(session->document().find_container(same.container_id) &&
                    !session->document().find_container(same.created.front()) &&
                    !session->calculated_boundaries().back().has_solid() &&
                    session->calculated_boundaries().back().mesh.triangles.empty(),
                "Deleting the first Flat must leave an active divider and empty material");
            check(workspace::step_part_document_history(live, document_id, false) &&
                    session->document().find_container(same.created.front()),
                "Undo did not restore the independent Flat");
            workspace::delete_part_history(live,document_id,kernel,
                same.container_id);
            check(session->document().find_container(same.created.front())&&
                    !session->document().removed_reference_states.contains(
                        same.created.front())&&
                    session->calculated_boundaries().back().
                        calculation_errors.empty(),
                "Deleting the divider invalidated its independent Flat");
            near(measured_volume(kernel,session->calculated_boundaries().back().
                body_outputs.at(source)),expected*7/3,.001);
            check(workspace::step_part_document_history(live,document_id,false)&&
                    session->document().find_container(same.container_id),
                "Sheet from Body deletion Undo did not restore the divider");
            near(measured_volume(kernel,session->calculated_boundaries().back().
                body_outputs.at(source)),expected*2/3,.001);
            check(workspace::step_part_document_history(live,document_id,true)&&
                    !session->document().find_container(same.container_id),
                "Sheet from Body deletion Redo retained the divider");
            for(const auto& created:same.created)
                workspace::delete_part_history(live,document_id,kernel,created);
            const auto& restored_output=session->calculated_boundaries().back().
                body_outputs.at(source)->mesh;
            check(!restored_output.triangle_references.empty()&&
                    std::ranges::all_of(
                        restored_output.triangle_references,
                        [](const auto& face){return face.valid();}),
                "Deleting generated sheets retained ownerless current faces");
            const auto& restored_faces=session->calculated_boundaries().back().
                mesh.original_references.triangle_references;
            const auto restored_seed=std::ranges::find_if(
                restored_faces,[&](const auto& face) {
                    return face.owner_id==feature.id&&face.surface&&
                        face.surface->kind==
                            kernel::SurfaceGeometry::Kind::Plane&&
                        face.sheet_role==kernel::SheetFaceRole::SideA;
                });
            check(restored_seed!=restored_faces.end(),
                "Deleting Sheet from Body and every generated sheet lost the original source face");
            const auto reconverted=workspace::prepare_sheet_from_body(
                session->document(),session->calculated_boundaries(),
                *restored_seed,2,kernel);
            check(!reconverted.created.empty()&&
                    reconverted.document.find_container(
                        reconverted.container_id),
                "Sheet from Body cannot run again after its divider and generated sheets were deleted");
        }
        if(direction==document::ExtrusionDirection::Forward) {
            auto inherited=part;
            document::set_sheet_metal_defaults(inherited,{2.5,.5});
            auto follows=workspace::prepare_sheet_from_body(
                inherited,cache,seed,99,kernel,false);
            const auto* boundary=follows.document.find_container(
                follows.container_id);
            check(boundary&&!boundary->sheet_from_body.thickness_override&&
                    std::abs(boundary->sheet_from_body.thickness-2.5)<1e-12,
                "Sheet from Body did not use the Part sheet thickness");
            check(std::ranges::all_of(follows.created,[&](const auto& id) {
                    const auto* feature=follows.document.find_container(id);
                    return feature&&feature->feature_kind==
                            document::FeatureKind::Flat&&
                        !feature->flat.thickness_override;
                }),"Generated sheet features did not inherit Part thickness");
            near(measured_volume(kernel,
                follows.calculated.back().body_outputs.at(source)),
                expected*2.5/3,.001);
            document::set_sheet_metal_defaults(follows.document,{4.,.5});
            follows.calculated=workspace::calculate_part_with_resolved_references(
                kernel,follows.document,&follows.calculated);
            near(measured_volume(kernel,
                follows.calculated.back().body_outputs.at(source)),
                expected*4/3,.001);
        }
        const auto target=part.body_history.create_body("Sheet");
        if(placed) {
            auto body=*part.body_history.find(target);body.scope.placement.x=-30;body.scope.placement.z=17;
            body.scope.placement.rotation_x=body.scope.placement.absolute_rotation_x=-31;
            body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=13;part.body_history.update_body(body);
        }
        const auto before=part.serialized();
        auto wrong_order=part;wrong_order.body_history.move_body(target,0);
        bool order_rejected=false;try{static_cast<void>(workspace::prepare_sheet_from_body(wrong_order,cache,seed,3,kernel));}catch(const std::invalid_argument&){order_rejected=true;}
        check(order_rejected,"Conversion accepted a source after its destination");
        auto converted=workspace::prepare_sheet_from_body(part,cache,seed,3,kernel);
        check(part.serialized()==before,"Conversion mutated its input");
        check(converted.created.size()==1&&converted.skipped==0,"Planar conversion created unexpected features");
        const auto* retained_source=converted.document.body_history.find(source);
        const auto* cross_boundary=converted.document.find_container(
            converted.container_id);
        check(cross_boundary&&
                cross_boundary->feature_kind==
                    document::FeatureKind::SheetFromBody&&
                !cross_boundary->sheet_from_body.replaces_body&&
                cross_boundary->sheet_from_body.source_body_id==source&&
                cross_boundary->sheet_from_body.generated_features==
                    converted.created&&
                cross_boundary->sheet_from_body.child_bindings.size()==1&&
                retained_source&&
                retained_source->visible&&!retained_source->suppressed&&
                std::ranges::find(converted.document.body_history.find(
                    target)->dependencies,source)!=
                    converted.document.body_history.find(target)->
                        dependencies.end(),
            "Cross-Body conversion did not retain its associative boundary and source Body");
        const auto& sketch=converted.document.sketches.back();
        check(sketch.bsplines.empty()&&sketch.arcs.size()>=4,"Native slots were replaced with splines");
        check(converted.document.history.back().placement.references.empty()&&sketch.external_references.empty(),"Root sheet retained source references");
        near(measured_volume(kernel,converted.calculated.back().body_outputs.at(target)),expected,.001);
        near(measured_volume(kernel,converted.calculated.back()),expected*2,.001);
        const auto& source_mesh=cache.back().body_outputs.at(source)->mesh;
        const auto& converted_mesh=converted.calculated.back().body_outputs.at(target)->mesh;
        for(const auto& point:source_mesh.points) {
            double error=INFINITY;for(const auto& other:converted_mesh.points)
                error=std::min(error,std::hypot(point.position.x-other.position.x,point.position.y-other.position.y,point.position.z-other.position.z));
            near(error,0,.001);
        }
        auto exported=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        check(exported.contour.bsplines.empty(),"Circular DXF contour became splines");
        check(exported.contour.arcs.size()>=4,"DXF lost the slot arcs");near(exported.volume,expected,.001);
        const auto serialized=converted.document.serialized();
        auto reopened=document::PartDocument::from_serialized(serialized);
        const auto recalculated=workspace::calculate_part_with_resolved_references(kernel,reopened);
        near(measured_volume(kernel,recalculated.back().body_outputs.at(target)),expected,.001);
        near(measured_volume(kernel,recalculated.back()),expected*2,.001);
        workspace::Workspace conversion_live;
        const auto conversion_document_id=reopened.document_id;
        conversion_live.add_part(reopened,recalculated);
        workspace::delete_part_history(conversion_live,
            conversion_document_id,kernel,converted.created.front());
        auto& conversion_session=conversion_live.open_part(
            conversion_document_id)->session;
        const auto* unchanged_source=conversion_session.document().
            body_history.find(source);
        check(unchanged_source&&unchanged_source->visible&&
                !unchanged_source->suppressed&&
                !conversion_session.document().find_container(
                    converted.created.front()),
            "Deleting the cross-Body Flat changed its independent source Body");
        near(measured_volume(kernel,
            conversion_session.calculated_boundaries().back()),expected,.001);
        check(workspace::step_part_document_history(conversion_live,
                    conversion_document_id,false)&&
                conversion_session.document().find_container(
                    converted.created.front())&&
                conversion_session.document().body_history.find(source)->visible,
            "Cross-Body Flat deletion Undo did not restore both Bodies");
        near(measured_volume(kernel,
            conversion_session.calculated_boundaries().back()),expected*2,.001);
        check(workspace::set_part_history_suppressed(conversion_live,
                    conversion_document_id,kernel,converted.created.front(),true)&&
                conversion_session.document().body_history.find(source)->visible&&
                !conversion_session.document().body_history.find(source)->suppressed,
            "Suppressing the cross-Body Flat changed its independent source Body");
        near(measured_volume(kernel,
            conversion_session.calculated_boundaries().back()),expected,.001);
        check(workspace::set_part_history_suppressed(conversion_live,
                    conversion_document_id,kernel,converted.created.front(),false)&&
                conversion_session.document().body_history.find(source)->visible&&
                !conversion_session.document().body_history.find(source)->suppressed,
            "Restoring the cross-Body Flat changed its independent source Body");
        near(measured_volume(kernel,
            conversion_session.calculated_boundaries().back()),expected*2,.001);
        auto body_deleted=conversion_session.document();
        body_deleted.erase_history_object(target);
        const auto* body_delete_source=body_deleted.body_history.find(source);
        check(body_delete_source&&body_delete_source->visible&&
                !body_delete_source->suppressed&&
                !body_deleted.body_history.find(target),
            "Deleting the cross-Body target changed its independent source Body");
        const auto body_delete_calculated=
            workspace::calculate_part_with_resolved_references(kernel,body_deleted);
        near(measured_volume(kernel,body_delete_calculated.back()),expected,.001);
        bool rejected=false;try{static_cast<void>(workspace::prepare_sheet_from_body(reopened,recalculated,seed,3,kernel));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Conversion accepted a non-empty target Body");
    }
    {
        // Imported manufacturing geometry may be thicker than the newly
        // authored sheet.  Recognition uses the measured STEP thickness,
        // while the generated Flat/Bend sequence uses the requested one.
        // The material-side cylinder must still reach both original axial
        // end edges; otherwise a 3 mm STEP reconstructed as 1 mm finishes
        // short of the supplier geometry.
        auto source_part=document::PartDocument::create_default();
        const auto source_body=source_part.body_history.create_body(
            "Three millimetre imported sheet");
        document::set_sheet_metal_defaults(source_part,
            {3.,0.318309886184});
        auto flat=document::PartDocument::create_sketch_container();
        flat.feature_kind=document::FeatureKind::Flat;
        flat.flat.thickness_override=true;
        flat.flat.thickness=3;
        flat.flat.direction=document::ExtrusionDirection::Reverse;
        auto profile=sketcher::Sketch::create_default();
        profile.owner_container_id=flat.id;
        flat.flat.sketch_id=profile.id;
        static_cast<void>(profile.add_rectangle(0,0,80,50));
        source_part.insert_history_entry(
            document::PartHistoryKind::Feature,flat.id);
        source_part.history.push_back(flat);
        source_part.sketches.push_back(profile);
        auto source_cache=workspace::calculate_part_with_resolved_references(
            kernel,source_part);
        const auto joining_edge=std::ranges::find_if(
            source_cache.back().mesh.original_references.edges,
            [](const auto& edge) {
                return kernel::sheet_edge_role(edge)==
                        kernel::SheetEdgeRole::Boundary&&
                    edge.points.size()>=2&&
                    std::ranges::all_of(edge.points,[](auto point) {
                        return std::abs(point.y-50)<1e-6&&
                            std::abs(point.z)<1e-6;
                    });
            });
        check(joining_edge!=source_cache.back().mesh.
                original_references.edges.end(),
            "Three millimetre STEP fixture lacks its Bend edge");
        auto bend=document::PartDocument::create_sketch_container();
        bend.feature_kind=document::FeatureKind::Bend;
        bend.bend.sheet_attachment=true;
        bend.bend.thickness_override=true;
        bend.bend.thickness=3;
        bend.bend.radius=6;
        bend.bend.angle_degrees=90;
        auto section=sketcher::Sketch::create_default();
        section.owner_container_id=bend.id;
        bend.bend.sketch_id=section.id;
        document::initialize_bend_start_profile(section,80);
        bend.placement.references=
            document::bend_sheet_references(*joining_edge);
        source_part.insert_history_entry(
            document::PartHistoryKind::Feature,bend.id);
        source_part.history.push_back(bend);
        source_part.sketches.push_back(section);
        source_cache=workspace::calculate_part_with_resolved_references(
            kernel,source_part,&source_cache);
        auto wall=document::PartDocument::create_sketch_container();
        wall.feature_kind=document::FeatureKind::Flat;
        wall.flat.sheet_attachment=true;
        wall.flat.thickness_override=true;
        wall.flat.thickness=3;
        wall.flat.direction=document::ExtrusionDirection::Reverse;
        auto wall_profile=sketcher::Sketch::create_default();
        wall_profile.owner_container_id=wall.id;
        wall.flat.sketch_id=wall_profile.id;
        bool attached{};
        for(const auto& candidate:
                source_cache.back().mesh.original_references.edges) {
            if(candidate.reference.owner_id!=bend.id)continue;
            if(std::ranges::none_of(
                    candidate.edge_treatment_side_references,
                    [](const auto& face) {
                        return face.semantic_key.starts_with(
                            "sweep:cap:end:");
                    }))
                continue;
            try {
                wall.placement.references=
                    document::flat_sheet_references(candidate);
            } catch(const std::invalid_argument&) {continue;}
            const auto first=candidate.points.front();
            const auto last=candidate.points.back();
            const double width=std::hypot(first.x-last.x,
                first.y-last.y,first.z-last.z);
            static_cast<void>(wall_profile.add_rectangle(0,0,width,30));
            attached=true;
            break;
        }
        check(attached,
            "Three millimetre STEP fixture lacks its continuation edge");
        source_part.insert_history_entry(
            document::PartHistoryKind::Feature,wall.id);
        source_part.history.push_back(wall);
        source_part.sketches.push_back(wall_profile);
        source_cache=workspace::calculate_part_with_resolved_references(
            kernel,source_part,&source_cache);
        check(source_cache.back().calculation_errors.empty()&&
                source_cache.back().body_outputs.at(source_body)->has_solid(),
            "Three millimetre STEP fixture produced no source sheet");

        QTemporaryDir directory(
            QStringLiteral("build/sheet-thickness-step-XXXXXX"));
        check(directory.isValid(),
            "Cannot isolate different-thickness STEP fixture");
        const auto step_path=std::filesystem::u8path(
            directory.path().toStdString())/"three-millimetre-sheet.step";
        kernel.export_step(std::vector<kernel::PlacedBody>{{
                source_cache.back().body_outputs.at(source_body).get(),{},{}}},
            step_path.string());
        auto imported=interchange::import_step_part(
            document::PartDocument::create_default(),{},step_path);
        const auto& imported_faces=imported.calculated.back().mesh.
            original_references.triangle_references;
        const auto seed=std::ranges::max_element(imported_faces,{},
            [](const auto& face) {
                return face.surface&&face.surface->kind==
                        kernel::SurfaceGeometry::Kind::Plane?
                    face.measured_area.value_or(0):0;
            });
        check(seed!=imported_faces.end()&&seed->surface&&
                seed->surface->kind==kernel::SurfaceGeometry::Kind::Plane,
            "Imported three millimetre sheet lacks its planar seed");
        near(workspace::suggest_sheet_thickness(
            imported.calculated.back().mesh,*seed),3,.001);
        const auto converted=workspace::prepare_sheet_from_body(
            imported.document,imported.calculated,*seed,1,kernel);
        check(converted.created.size()==3&&converted.skipped==0&&
                converted.calculated.back().calculation_errors.empty(),
            "Three to one millimetre STEP conversion lost a Flat or Bend");
        const auto* replacement=converted.document.find_container(
            converted.container_id);
        check(replacement&&replacement->sheet_from_body.child_bindings.empty()&&
                !replacement->sheet_from_body.source_face.reference.valid()&&
                replacement->sheet_from_body.included_faces.empty()&&
                replacement->sheet_from_body.rip_sketches.empty()&&
                replacement->sheet_from_body.generated_features==
                    converted.created,
            "Imported STEP conversion became an associative solid conversion");

        const auto same_axis=[](const kernel::SurfaceGeometry& first,
                const kernel::SurfaceGeometry& second) {
            const auto a=kernel::sheet_material::unit(first.axis);
            const auto b=kernel::sheet_material::unit(second.axis);
            const auto delta=kernel::sheet_material::sub(
                first.origin,second.origin);
            const auto across=kernel::sheet_material::cross(delta,a);
            return std::abs(std::abs(
                       kernel::sheet_material::dot(a,b))-1)<1e-7&&
                std::sqrt(kernel::sheet_material::dot(across,across))<1e-5;
        };
        const auto reaches_source_ends=[&](const auto& calculated) {
            const auto active=converted.document.body_history.active_body_id();
            const auto& result_faces=calculated.back().body_outputs.at(active)->
                mesh.original_references.triangle_references;
            for(const auto& source_face:imported_faces) {
                if(!source_face.surface||source_face.surface->kind!=
                        kernel::SurfaceGeometry::Kind::Cylinder)
                    continue;
                for(const auto& result_face:result_faces) {
                    if(!result_face.surface||result_face.surface->kind!=
                            kernel::SurfaceGeometry::Kind::Cylinder)
                        continue;
                    const auto& source_surface=*source_face.surface;
                    const auto& result_surface=*result_face.surface;
                    if(same_axis(source_surface,result_surface)&&
                            std::abs(source_surface.radius-
                                result_surface.radius)<1e-5&&
                            std::abs(source_surface.axial_min-
                                result_surface.axial_min)<1e-5&&
                            std::abs(source_surface.axial_max-
                                result_surface.axial_max)<1e-5)
                        return true;
                }
            }
            return false;
        };
        check(reaches_source_ends(converted.calculated),
            "One millimetre Bend does not reach the original STEP end edges");
        auto reopened=document::PartDocument::from_serialized(
            converted.document.serialized());
        const auto cold=workspace::calculate_part_with_resolved_references(
            kernel,reopened);
        check(cold.back().calculation_errors.empty()&&
                reaches_source_ends(cold),
            "Different-thickness STEP Bend changed after save and reopen");
    }
    {
        // A centred source face should seed the new root Flat at the Part
        // Origin planes, not at an arbitrary OCCT face-frame origin/edge.
        auto part=document::PartDocument::create_default();
        const auto source=part.body_history.create_body("Centred source");
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=flat.id;flat.flat.sketch_id=profile.id;
        flat.flat.thickness_override=true;flat.flat.thickness=2;
        static_cast<void>(profile.add_rectangle(-40,-20,40,20));
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);part.history.push_back(flat);part.sketches.push_back(profile);
        const auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& faces=cache.back().body_outputs.at(source)->mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                face.sheet_role==kernel::SheetFaceRole::SideA;
        });
        check(seed!=faces.end(),"Centred sheet fixture has no planar face");
        const auto target=part.body_history.create_body("Centred conversion");
        const auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,2,kernel);
        const auto& root=converted.document.sketches.back();
        near(root.resolved_origin.x,0);near(root.resolved_origin.y,0);
        check(std::abs(std::abs(root.resolved_x_axis.x)+std::abs(root.resolved_x_axis.y)-1)<1e-7&&
              std::abs(root.resolved_x_axis.z)<1e-7,
            "Sheet from Body did not align its root X axis with a Part Origin plane");
        check(converted.created.size()==1&&converted.document.body_history.owner(converted.created.front())->scope.id==target,
            "Centred Sheet from Body changed its target ownership");
        const auto reopened=document::PartDocument::from_serialized(converted.document.serialized());
        const auto& persisted=reopened.sketches.back();
        near(persisted.resolved_origin.x,0);near(persisted.resolved_origin.y,0);
        near(persisted.resolved_x_axis.x,root.resolved_x_axis.x);near(persisted.resolved_x_axis.y,root.resolved_x_axis.y);
    }
    {
        auto part=document::PartDocument::create_default();const auto source=part.body_history.create_body("Bent source");
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=flat.id;flat.flat.sketch_id=profile.id;
        flat.flat.thickness_override=true;flat.flat.thickness=2;flat.flat.direction=document::ExtrusionDirection::Reverse;
        static_cast<void>(profile.add_segment(0,0,60,0));static_cast<void>(profile.add_segment(60,0,60,40));
        static_cast<void>(profile.add_segment(60,40,0,40));static_cast<void>(profile.add_segment(0,40,0,0));
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);part.history.push_back(flat);part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& edges=cache.back().mesh.original_references.edges;
        const auto edge=std::ranges::find_if(edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
            std::ranges::all_of(e.points,[](auto p){return std::abs(p.y-40)<1e-6&&std::abs(p.z)<1e-6;});});
        check(edge!=edges.end(),"Bend fixture has no joining edge");
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        auto section=sketcher::Sketch::create_default();section.owner_container_id=bend.id;document::initialize_bend_start_profile(section,60);
        bend.bend.sketch_id=section.id;bend.bend.sheet_attachment=true;bend.bend.radius=4;bend.bend.angle_degrees=90;
        bend.placement.references=document::bend_sheet_references(*edge);
        part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);part.history.push_back(bend);part.sketches.push_back(section);
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache);
        auto wall=document::PartDocument::create_sketch_container();wall.feature_kind=document::FeatureKind::Flat;
        auto wall_profile=sketcher::Sketch::create_default();wall_profile.owner_container_id=wall.id;wall.flat.sketch_id=wall_profile.id;
        wall.flat.sheet_attachment=true;wall.flat.direction=document::ExtrusionDirection::Reverse;
        double end_width=0;
        for(const auto& candidate:cache.back().mesh.original_references.edges)if(candidate.reference.owner_id==bend.id) {
            if(std::ranges::none_of(candidate.edge_treatment_side_references,[](const auto& face){return face.semantic_key.starts_with("sweep:cap:end:");}))continue;
            try{wall.placement.references=document::flat_sheet_references(candidate);}
            catch(const std::invalid_argument&){continue;}
            const auto a=candidate.points.front(),b=candidate.points.back();end_width=std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);break;
        }
        check(end_width>0,"Bend has no continuation edge");
        static_cast<void>(wall_profile.add_rectangle(0,0,end_width,25));
        part.insert_history_entry(document::PartHistoryKind::Feature,wall.id);part.history.push_back(wall);part.sketches.push_back(wall_profile);
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[&](const auto& f){return f.owner_id==flat.id&&f.sheet_role==kernel::SheetFaceRole::SideA;});
        check(seed!=faces.end(),"Bend fixture has no base skin");
        {
            auto invalid=cache;auto input=invalid.back().body_outputs.at(source).get();
            for(auto* list:{&input.mesh.triangle_references,&input.mesh.original_references.triangle_references})
                for(auto& face:*list)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
                    auto surface=*face.surface;surface.radius=1;
                    face.surface=std::make_shared<const kernel::SurfaceGeometry>(std::move(surface));
                }
            invalid.back().body_outputs[source]=kernel::BodySnapshot(std::move(input));
            const auto before=part.serialized();bool rejected=false;
            std::size_t invalid_created{};
            try{invalid_created=workspace::prepare_sheet_from_body(
                    part,invalid,*seed,2,kernel).created.size();}
            catch(const std::invalid_argument&){rejected=true;}
            if(!rejected||part.serialized()!=before)
                throw std::runtime_error(
                    "Unsupported branch committed a partial conversion: created="+
                    std::to_string(invalid_created));
        }
        {
            auto same=workspace::prepare_sheet_from_body(part,cache,*seed,2,kernel);
            check(same.created.size()==3&&same.skipped==0,"Same-Body conversion lost a connected wall or bend");
            near(measured_volume(kernel,same.calculated.back().body_outputs.at(source)),
                measured_volume(kernel,cache.back().body_outputs.at(source)),.01);
            auto cold=workspace::calculate_part_with_resolved_references(kernel,same.document);
            near(measured_volume(kernel,cold.back().body_outputs.at(source)),
                measured_volume(kernel,cache.back().body_outputs.at(source)),.01);
            const auto dxf=workspace::prepare_sheet_dxf(same.document,cold,kernel);
            check(dxf.export_options.bend_axis_ids.size()==1,"Same-Body DXF retained source bend material");
            {
                workspace::Workspace live;
                live.add_part(same.document);
                auto* state=live.open_part(same.document.document_id);
                state->session.update_calculated_boundaries(same.calculated);
                const auto detached_id=same.created.at(1);
                const auto revision=state->session.revision();
                check(workspace::explode_sheet_from_body(live,kernel,
                        same.document.document_id,same.container_id,
                        detached_id)&&state->session.revision()==revision+1,
                    "Partial Sheet from Body explode was not one transaction");
                const auto* partially_exploded=state->session.document().
                    find_container(same.container_id);
                const auto detached=std::ranges::find(
                    partially_exploded->sheet_from_body.child_bindings,
                    detached_id,
                    &document::SheetFromBodyChildBinding::feature_id);
                check(detached!=partially_exploded->sheet_from_body.
                        child_bindings.end()&&detached->detached&&
                        state->session.document().find_container(detached_id),
                    "Partial Sheet from Body explode lost its native child");
                near(measured_volume(kernel,state->session.
                    calculated_boundaries().back().body_outputs.at(source)),
                    measured_volume(kernel,same.calculated.back().
                        body_outputs.at(source)),.01);
                const auto no_op_revision=state->session.revision();
                const auto* no_op_cache=state->session.
                    calculated_boundaries().data();
                check(!workspace::explode_sheet_from_body(live,kernel,
                        same.document.document_id,same.container_id,
                        detached_id)&&
                        state->session.revision()==no_op_revision&&
                        state->session.calculated_boundaries().data()==
                            no_op_cache,
                    "Repeated partial explode recalculated or created Undo");
                check(state->session.undo(),
                    "Partial Sheet from Body explode has no Undo");
                const auto* restored=state->session.document().find_container(
                    same.container_id);
                check(restored&&std::ranges::none_of(
                        restored->sheet_from_body.child_bindings,
                        [](const auto& binding){return binding.detached;}),
                    "Undo did not restore the Sheet from Body association");

                const auto all_revision=state->session.revision();
                check(workspace::explode_sheet_from_body(live,kernel,
                        same.document.document_id,same.container_id)&&
                        state->session.revision()!=all_revision,
                    "Complete Sheet from Body explode was not one transaction");
                const auto& exploded=state->session.document();
                check(!exploded.find_container(same.container_id)&&
                        std::ranges::all_of(same.created,[&](const auto& id) {
                            return exploded.find_container(id)!=nullptr;
                        })&&exploded.find_container(flat.id)->suppressed&&
                        exploded.find_container(bend.id)->suppressed&&
                        exploded.find_container(wall.id)->suppressed,
                    "Complete same-Body explode restored source material or lost generated features");
                const double exploded_volume=measured_volume(kernel,
                    state->session.calculated_boundaries().back().
                        body_outputs.at(source));
                near(exploded_volume,measured_volume(kernel,
                    same.calculated.back().body_outputs.at(source)),.01);
                auto exploded_reopened=document::PartDocument::
                    from_serialized(exploded.serialized());
                const auto exploded_cold=workspace::
                    calculate_part_with_resolved_references(
                        kernel,exploded_reopened);
                near(measured_volume(kernel,exploded_cold.back().
                        body_outputs.at(source)),exploded_volume,.01);
                check(state->session.undo()&&state->session.document().
                        find_container(same.container_id)&&
                        !state->session.document().find_container(flat.id)->
                            suppressed,
                    "Undo did not restore complete Sheet from Body explode");
            }
        }
        const auto target=part.body_history.create_body("Converted");
        const auto source_bend_face=std::ranges::find_if(faces,[&](const auto& face) {
            return face.owner_id==bend.id&&face.sheet_role==kernel::SheetFaceRole::SideA&&face.surface&&
                face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;
        });
        check(source_bend_face!=faces.end(),"Bent source has no outer cylindrical sheet face");
        const auto limited=workspace::prepare_sheet_from_body(
            part,cache,*seed,2,kernel,true,{*source_bend_face});
        check(limited.created.size()==3&&
                limited.document.find_container(limited.created.front())->
                    feature_kind==document::FeatureKind::Flat&&
                limited.document.find_container(limited.created[1])->
                    feature_kind==document::FeatureKind::Bend&&
                limited.document.find_container(limited.created.back())->
                    feature_kind==document::FeatureKind::Flat,
            "Included Sheet-from-Body Bend did not retain its complete transition");
        const auto* limited_boundary=limited.document.find_container(
            limited.container_id);
        check(limited_boundary&&limited_boundary->sheet_from_body.
                included_faces.size()==1,
            "Included Sheet-from-Body face was not persisted");
        auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,2,kernel);
        check(converted.created.size()==3&&converted.skipped==0,"Converter failed to follow the cylinder and its next wall");
        const double expected_radius=source_bend_face->surface->radius-
            (source_bend_face->surface->reversed?0:2);
        int flats=0,bends=0;
        for(const auto& owner:converted.created) {
            const auto& item=*converted.document.find_container(owner);
            if(item.feature_kind==document::FeatureKind::Flat)++flats;
            if(item.feature_kind==document::FeatureKind::Bend) {
                ++bends;
                near(item.bend.radius,expected_radius,.001);
                const auto trajectory=sketcher::Sketch::from_serialized(item.bend.auxiliary_sketches[0]);
                check(trajectory.arcs.size()==1&&std::ranges::none_of(trajectory.segments,[](const auto& line){return !line.construction;}),
                    "Sheet from Body must create arc-only Bends with separate Flat walls");
            }
        }
        check(flats==2&&bends==1,"Sheet from Body merged a flat wall into the Bend");
        {
            workspace::Workspace live;
            live.add_part(converted.document);
            auto* state=live.open_part(converted.document.document_id);
            state->session.update_calculated_boundaries(
                converted.calculated);
            check(workspace::explode_sheet_from_body(live,kernel,
                    converted.document.document_id,
                    converted.container_id),
                "Cross-Body Sheet from Body could not be exploded");
            const auto& exploded=state->session.document();
            check(!exploded.find_container(converted.container_id)&&
                    !exploded.find_container(flat.id)->suppressed&&
                    !exploded.find_container(bend.id)->suppressed&&
                    !exploded.find_container(wall.id)->suppressed&&
                    std::ranges::all_of(converted.created,
                        [&](const auto& id) {
                            return exploded.find_container(id)!=nullptr;
                        }),
                "Cross-Body explode changed its source or lost generated features");
            near(measured_volume(kernel,state->session.
                    calculated_boundaries().back().body_outputs.at(target)),
                measured_volume(kernel,converted.calculated.back().
                    body_outputs.at(target)),.01);
            check(state->session.undo()&&state->session.document().
                    find_container(converted.container_id),
                "Cross-Body Sheet from Body explode has no Undo");
        }
        {
            // Every generated region remains tied to the native source face,
            // including analytic bend radius changes. Child and Sketch IDs
            // are authored once and must survive repeated source edits.
            auto radius_linked=converted;
            const auto generated_ids=radius_linked.created;
            std::vector<std::string> sketch_ids;
            for(const auto& id:generated_ids) {
                const auto* child=radius_linked.document.find_container(id);
                sketch_ids.push_back(child->feature_kind==
                        document::FeatureKind::Flat?
                    child->flat.sketch_id:child->bend.sketch_id);
            }
            const auto update_source_radius=[&](double radius) {
                auto* source_bend=radius_linked.document.find_container(
                    bend.id);
                check(source_bend&&source_bend->feature_kind==
                        document::FeatureKind::Bend,
                    "Associative radius source Bend is unavailable");
                source_bend->bend.radius=radius;
                const auto source_sketch=std::ranges::find(
                    radius_linked.document.sketches,
                    source_bend->bend.sketch_id,&sketcher::Sketch::id);
                check(source_sketch!=radius_linked.document.sketches.end(),
                    "Associative radius source Sketch is unavailable");
                document::prepare_bend_sketches(*source_bend,
                    *source_sketch,document::sheet_metal_defaults(
                        radius_linked.document));
                radius_linked.calculated=
                    workspace::calculate_part_with_resolved_references(
                        kernel,radius_linked.document,
                        &radius_linked.calculated);
                const auto* parent=radius_linked.document.find_container(
                    radius_linked.container_id);
                check(parent&&parent->sheet_from_body.generated_features==
                        generated_ids&&std::ranges::all_of(
                            parent->sheet_from_body.child_bindings,
                            [](const auto& binding) {
                                return binding.references_valid;
                            }),
                    "Source radius edit lost an associative region");
                bool checked_bend{};
                for(std::size_t index=0;index<generated_ids.size();++index) {
                    const auto* child=radius_linked.document.find_container(
                        generated_ids[index]);
                    check(child&&(child->feature_kind==
                            document::FeatureKind::Flat?
                        child->flat.sketch_id:child->bend.sketch_id)==
                            sketch_ids[index],
                        "Source radius edit changed a generated identity");
                    if(child->feature_kind==document::FeatureKind::Bend) {
                        near(child->bend.radius,radius,.001);
                        checked_bend=true;
                    }
                }
                check(checked_bend,
                    "Source radius edit lost its generated Bend");
                near(measured_volume(kernel,radius_linked.calculated.back().
                        body_outputs.at(target)),
                    measured_volume(kernel,radius_linked.calculated.back().
                        body_outputs.at(source)),.02);
            };
            update_source_radius(6);
            update_source_radius(7);
            const auto radius_settled=radius_linked.document.serialized();
            radius_linked.calculated=
                workspace::calculate_part_with_resolved_references(
                    kernel,radius_linked.document,
                    &radius_linked.calculated);
            check(radius_linked.document.serialized()==radius_settled,
                "Unchanged radius association rewrote the document");
            const auto radius_operations=
                document::solid_state_calculation_operations(
                    radius_linked.document,&radius_linked.calculated);
            const auto radius_fingerprints=
                kernel::history_fingerprints(radius_operations);
            for(std::size_t index=0;
                    index<radius_linked.calculated.size();++index)
                if(radius_linked.calculated[index].source_fingerprint!=
                        radius_fingerprints.at(index+1))
                    throw std::runtime_error(
                        "Radius association returned a stale boundary at "+
                        std::to_string(index)+" owner "+
                        radius_operations.at(index).owner_id);
            auto radius_reopened=document::PartDocument::from_serialized(
                radius_linked.document.serialized(
                    radius_linked.calculated));
            const auto radius_cold=
                workspace::calculate_part_with_resolved_references(
                    kernel,radius_reopened);
            const auto* radius_parent=radius_reopened.find_container(
                radius_linked.container_id);
            check(radius_parent&&radius_parent->sheet_from_body.
                    generated_features==generated_ids&&
                    radius_cold.back().calculation_errors.empty(),
                "Radius-linked Sheet from Body changed after reopen");
        }
        const double folded_volume=measured_volume(kernel,converted.calculated.back().body_outputs.at(target));
        near(folded_volume,measured_volume(kernel,cache.back().body_outputs.at(source)),.01);
        const auto dxf=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        check(dxf.export_options.bend_axis_ids.size()==1,"Unfolded sheet lost its bend axis");
        for(const auto& line:dxf.contour.segments)if(dxf.export_options.bend_axis_ids.contains(line.id)) {
            const auto* a=dxf.contour.find_point(line.first_point_id);const auto* b=dxf.contour.find_point(line.second_point_id);
            near(std::hypot(a->x-b->x,a->y-b->y),60,.001);
        }
        near(dxf.volume,folded_volume,.05);
        bool source_delete_rejected{};
        try {
            converted.document.erase_history_object(source);
        } catch(const std::invalid_argument&) {
            source_delete_rejected=true;
        }
        check(source_delete_rejected&&
                converted.document.body_history.find(source),
            "Associative Sheet from Body allowed its source Body to be deleted");
        converted.calculated=workspace::calculate_part_with_resolved_references(
            kernel,converted.document);
        near(measured_volume(kernel,converted.calculated.back().body_outputs.at(target)),folded_volume,.01);
        for(bool unfold:{true,false}) {
            auto state=document::PartDocument::create_sketch_container();state.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            converted.document.insert_history_entry(document::PartHistoryKind::Feature,state.id);converted.document.history.push_back(state);
            converted.calculated=workspace::calculate_part_with_resolved_references(kernel,converted.document,&converted.calculated);
            check(converted.calculated.back().calculation_errors.empty(),"Converted Bend failed a sheet-state operation");
            near(measured_volume(kernel,converted.calculated.back().body_outputs.at(target)),folded_volume,.05);
        }
        auto cut=document::PartDocument::create_sketch_container();cut.feature_kind=document::FeatureKind::Extrusion;
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
        cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        cut.extrusion.end_condition_forward=cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
        auto opening=sketcher::Sketch::create_default();opening.owner_container_id=cut.id;cut.extrusion.sketch_id=opening.id;
        static_cast<void>(opening.add_segment(10,10,15,10));static_cast<void>(opening.add_segment(15,10,15,15));
        static_cast<void>(opening.add_segment(15,15,10,15));static_cast<void>(opening.add_segment(10,15,10,10));
        converted.document.insert_history_entry(document::PartHistoryKind::Feature,cut.id);
        converted.document.history.push_back(cut);converted.document.sketches.push_back(opening);
        converted.calculated=workspace::calculate_part_with_resolved_references(kernel,converted.document,&converted.calculated);
        check(converted.calculated.back().calculation_errors.empty(),"Converted sheet rejected Sheet Cut");
        near(measured_volume(kernel,converted.calculated.back().body_outputs.at(target)),folded_volume-50,.05);
        const auto cut_dxf=workspace::prepare_sheet_dxf(converted.document,converted.calculated,kernel);
        near(cut_dxf.volume,folded_volume-50,.05);
        check(cut_dxf.contour.segments.size()==dxf.contour.segments.size()+4,"DXF omitted the Sheet Cut opening");
    }
    std::cout << "Sheet exchange contracts passed"
              << (include_oblique_corners ? "" : " (excluding oblique corners)") << '\n';
}
}

int main(int argc,char** argv) {
    try {
        if(argc==1){contract();return 0;}
        if (argc == 2 && std::string_view(argv[1]) == "--without-oblique-corners") {
            contract(false);
            return 0;
        }
        if(argc<2||argc>5)return 2;
        std::vector<zima::kernel::BodyResult> cache;
        zima::document::PartDocument part;
        try { part=zima::document::PartDocument::load(argv[1],&cache); }
        catch(const std::exception&) {
            // Diagnostic fixtures may intentionally carry a stale disposable
            // calculated cache. The authored native model remains the source
            // of truth; rebuild it without accepting that cache.
            cache.clear();part=zima::document::PartDocument::load(argv[1]);
            zima::kernel::OcctKernel regeneration;
            cache=zima::workspace::calculate_part_with_resolved_references(
                regeneration,part);
        }
        if(cache.empty()) {
            zima::kernel::OcctKernel regeneration;
            cache=zima::workspace::calculate_part_with_resolved_references(
                regeneration,part);
        }
        if(argc==5&&std::string_view(argv[2])=="trim-copy") {
            const auto retained=static_cast<std::size_t>(std::stoull(argv[3]));
            check(retained<=part.history.size(),
                "Trim-copy cursor exceeds the Part history");
            while(part.history.size()>retained)
                part.erase_history_object(part.history.back().id);
            // The archive may carry intentionally compact lazy boundaries
            // without per-Body outputs.  A trimmed diagnostic fixture must be
            // independently calculated before it is used as a conversion
            // source; resizing that persisted cache can retain an empty Body
            // map and hide the geometry under test.
            zima::kernel::OcctKernel kernel;
            cache=zima::workspace::calculate_part_with_resolved_references(
                kernel,part);
            check(!cache.empty()&&cache.back().calculation_errors.empty(),
                "Trimmed diagnostic source did not regenerate");
            part.save(argv[4],cache);
            std::cout<<"trimmed_features="<<part.history.size()
                <<" output="<<argv[4]<<'\n';
            return 0;
        }
        const std::string_view diagnostic=argc==3?std::string_view(argv[2]):
            std::string_view{};
        if(diagnostic=="cache-boundaries") {
            for(std::size_t index=0;index<cache.size();++index) {
                std::cout<<"boundary["<<index<<"] outputs=";
                for(const auto& [body,output]:cache[index].body_outputs)
                    std::cout<<body<<":faces="
                        <<output->mesh.triangle_references.size()<<',';
                std::cout<<'\n';
            }
            return 0;
        }
        if(diagnostic=="flat-profiles") {
            for(const auto& feature:part.history) {
                if(feature.feature_kind!=document::FeatureKind::Flat)continue;
                const auto sketch=std::ranges::find(part.sketches,
                    feature.flat.sketch_id,&sketcher::Sketch::id);
                if(sketch==part.sketches.end())continue;
                std::cout<<"flat="<<feature.name<<':'<<feature.id<<'\n';
                for(const auto& segment:sketch->segments) {
                    if(segment.construction)continue;
                    const auto* first=sketch->find_point(
                        segment.first_point_id);
                    const auto* last=sketch->find_point(
                        segment.second_point_id);
                    if(!first||!last)continue;
                    const auto a=sketch->world_point(first->x,first->y);
                    const auto b=sketch->world_point(last->x,last->y);
                    std::cout<<"  "<<a.x<<','<<a.y<<','<<a.z<<" -> "
                        <<b.x<<','<<b.y<<','<<b.z<<'\n';
                }
            }
            return 0;
        }
        if(diagnostic=="bend-inner-gaps") {
            struct BendEndCurve {
                std::string owner;
                std::string role;
                std::vector<zima::kernel::Vec3> points;
            };
            std::vector<BendEndCurve> end_curves;
            const auto defaults=zima::document::sheet_metal_defaults(part);
            for(const auto& feature:part.history) {
                if(feature.feature_kind!=zima::document::FeatureKind::Bend)
                    continue;
                const auto sketch=std::ranges::find(part.sketches,
                    feature.bend.sketch_id,&zima::sketcher::Sketch::id);
                check(sketch!=part.sketches.end(),
                    "Diagnostic Bend lost its profile Sketch");
                const auto preview=zima::document::bend_preview(
                    feature,*sketch,defaults);
                for(const auto& edge:preview.edges)
                    for(const auto* role:{"outer-first","outer-last",
                            "inner-first","inner-last"})
                        if(edge.reference.semantic_key.find(role)!=
                                std::string::npos)
                            end_curves.push_back({feature.id,role,edge.points});
            }
            const auto point_distance=[](const auto& a,const auto& b) {
                return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
            };
            const auto curve_gap=[&](const auto& a,const auto& b) {
                double result{};
                for(const auto* first:{&a,&b}) {
                    const auto* second=first==&a?&b:&a;
                    for(const auto& point:*first) {
                        double nearest=INFINITY;
                        for(const auto& other:*second)
                            nearest=std::min(nearest,
                                point_distance(point,other));
                        result=std::max(result,nearest);
                    }
                }
                return result;
            };
            for(const auto& curve:end_curves) {
                double nearest=INFINITY;
                double endpoint_nearest=INFINITY;
                double startpoint_nearest=INFINITY;
                std::string nearest_owner;
                for(const auto& other:end_curves)
                    if(other.owner!=curve.owner&&
                            other.role.starts_with(
                                curve.role.starts_with("outer")?
                                    "outer":"inner")) {
                        const double gap=curve_gap(curve.points,other.points);
                        if(gap<nearest) {
                            nearest=gap;
                            nearest_owner=other.owner;
                        }
                        if(!curve.points.empty()&&!other.points.empty())
                            startpoint_nearest=std::min(startpoint_nearest,
                                point_distance(curve.points.front(),
                                    other.points.front()));
                        if(!curve.points.empty()&&!other.points.empty())
                            endpoint_nearest=std::min(endpoint_nearest,
                                point_distance(curve.points.back(),
                                    other.points.back()));
                    }
                std::cout<<"bend_inner="<<curve.owner<<':'<<curve.role
                    <<" nearest="<<nearest
                    <<" startpoint_nearest="<<startpoint_nearest
                    <<" endpoint_nearest="<<endpoint_nearest
                    <<" other="<<nearest_owner<<'\n';
            }
            return 0;
        }
        if(diagnostic=="warm-unbend") {
            zima::kernel::OcctKernel kernel;
            zima::workspace::Workspace live;
            const auto document_id=part.document_id;
            cache=zima::workspace::calculate_part_with_resolved_references(
                kernel,part,&cache);
            live.add_part(part,cache);
            auto feature=zima::document::PartDocument::
                create_sketch_container();
            feature.feature_kind=zima::document::FeatureKind::Unbend;
            feature.name="Unbend performance probe";
            feature.sheet_state.all=true;
            const auto started=std::chrono::steady_clock::now();
            check(zima::workspace::commit_sheet_state(
                    live,kernel,document_id,std::move(feature)),
                "Unbend performance probe did not commit");
            const auto milliseconds=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            const auto& session=live.open_part(document_id)->session;
            std::cout<<"unbend milliseconds="<<milliseconds
                <<" operations="<<session.document().history.size()
                <<" errors="<<session.calculated_boundaries().back().
                    calculation_errors.size()<<'\n';
            return 0;
        }
        if(diagnostic=="warm-sheet-roundtrip") {
            zima::kernel::OcctKernel kernel;
            zima::workspace::Workspace live;
            const auto document_id=part.document_id;
            cache=zima::workspace::calculate_part_with_resolved_references(
                kernel,part,&cache);
            live.add_part(part,cache);
            std::vector<zima::document::HistoryContainer> authored_bends;
            for(const auto& candidate:part.history)
                if(candidate.feature_kind==zima::document::FeatureKind::Bend)
                    authored_bends.push_back(candidate);
            const auto bend_definition_unchanged=[](const auto* retained,
                    const auto& authored) {
                if(!retained)return false;
                const auto& current=retained->bend;
                const auto& original=authored.bend;
                return retained->id==authored.id&&
                    retained->feature_id==authored.feature_id&&
                    retained->feature_parent_id==authored.feature_parent_id&&
                    retained->feature_kind==authored.feature_kind&&
                    retained->container_origin==authored.container_origin&&
                    current.corner==original.corner&&
                    current.corner_gap==original.corner_gap&&
                    current.sketch_id==original.sketch_id&&
                    current.sheet_attachment==original.sheet_attachment&&
                    current.radius==original.radius&&
                    current.angle_degrees==original.angle_degrees&&
                    current.thickness==original.thickness&&
                    current.k_factor==original.k_factor&&
                    current.thickness_override==original.thickness_override&&
                    current.k_factor_override==original.k_factor_override&&
                    current.radius_follows_thickness==
                        original.radius_follows_thickness&&
                    zima::document::bend_profile_extensions(*retained)==
                        zima::document::bend_profile_extensions(authored)&&
                    retained->placement.references==
                        authored.placement.references;
            };
            const auto change_state=[&](zima::document::FeatureKind kind,
                    const char* name) {
                auto feature=zima::document::PartDocument::
                    create_sketch_container();
                feature.feature_kind=kind;
                feature.name=name;
                feature.sheet_state.all=true;
                const auto started=std::chrono::steady_clock::now();
                check(zima::workspace::commit_sheet_state(
                        live,kernel,document_id,std::move(feature)),
                    "Sheet-state round trip did not commit");
                return std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
            };
            const double unbend_ms=change_state(
                zima::document::FeatureKind::Unbend,
                "Unbend round-trip probe");
            const double bend_back_ms=change_state(
                zima::document::FeatureKind::BendBack,
                "Bend Back round-trip probe");
            const auto& session=live.open_part(document_id)->session;
            check(session.calculated_boundaries().back().calculation_errors.
                    empty(),
                "Sheet-state round trip produced a calculation error");
            for(const auto& authored:authored_bends) {
                const auto* retained=session.document().find_container(
                    authored.id);
                check(bend_definition_unchanged(retained,authored),
                    "Sheet-state round trip changed an authored Bend");
            }
            const auto serialized=session.document().serialized(
                session.calculated_boundaries());
            auto reopened=zima::document::PartDocument::from_serialized(
                serialized);
            zima::kernel::OcctKernel cold_kernel;
            const auto cold=zima::workspace::
                calculate_part_with_resolved_references(cold_kernel,reopened);
            check(!cold.empty()&&cold.back().calculation_errors.empty(),
                "Sheet-state round trip failed cold native regeneration");
            for(const auto& authored:authored_bends) {
                const auto* retained=reopened.find_container(authored.id);
                check(bend_definition_unchanged(retained,authored),
                    "Native reopen changed an authored Bend");
            }
            std::cout<<"unbend_ms="<<unbend_ms
                <<" bend_back_ms="<<bend_back_ms
                <<" bends="<<authored_bends.size()
                <<" operations="<<session.document().history.size()
                <<" cold_errors="<<cold.back().calculation_errors.size()
                <<'\n';
            return 0;
        }
        const bool warm_delete=diagnostic.starts_with("warm-delete-name=");
        if(diagnostic.starts_with("delete-name=")||warm_delete) {
            const std::string name(diagnostic.substr(
                warm_delete?17:12));
            const auto feature=std::ranges::find(part.history,name,
                &zima::document::HistoryContainer::name);
            check(feature!=part.history.end(),
                "Requested diagnostic deletion feature is unavailable");
            const auto feature_id=feature->id;
            zima::kernel::OcctKernel kernel;
            zima::workspace::Workspace live;
            const auto document_id=part.document_id;
            if(warm_delete)
                cache=zima::workspace::calculate_part_with_resolved_references(
                    kernel,part,&cache);
            live.add_part(part,cache);
            const auto plan=zima::workspace::plan_history_deletion(
                part,feature_id);
            const auto started=std::chrono::steady_clock::now();
            zima::workspace::delete_part_history(
                live,document_id,kernel,feature_id);
            const auto milliseconds=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            const auto& session=live.open_part(document_id)->session;
            std::cout<<"delete_name="<<name
                <<" milliseconds="<<milliseconds
                <<" affected="<<plan.affected.size()
                <<" unresolved="<<session.document().removed_reference_states.size()
                <<" errors="<<session.calculated_boundaries().back().calculation_errors.size()
                <<'\n';
            for(const auto& id:plan.affected) {
                const auto* retained=session.document().find_container(id);
                std::cout<<"affected_id="<<id
                    <<" name="<<(retained?retained->name:std::string{"<missing>"})
                    <<'\n';
            }
            return 0;
        }
        std::cout<<"Part "<<part.name<<" features="<<part.history.size()<<" bodies="<<part.body_history.bodies().size()<<" cached="<<cache.size()<<'\n';
        const auto diagnostic_geometry=
            zima::workspace::construction_reference_source_geometry(cache);
        for(std::size_t index=0;index<part.history.size();++index) {
            const auto& feature=part.history[index];
            std::cout<<"history["<<index<<"] kind="<<static_cast<int>(feature.feature_kind)
                <<" name="<<feature.name<<" id="<<feature.id;
            if(feature.feature_kind==zima::document::FeatureKind::Bend) {
                const auto extensions=
                    zima::document::bend_profile_extensions(feature);
                std::cout<<" attached="<<feature.bend.sheet_attachment
                    <<" radius="<<feature.bend.radius
                    <<" angle="<<feature.bend.angle_degrees
                    <<" corner="<<feature.bend.corner[0]<<','
                    <<feature.bend.corner[1]
                    <<" extensions="<<extensions[0]<<','
                    <<extensions[1];
            }
            if(feature.feature_kind==zima::document::FeatureKind::Fillet)
                {std::cout<<" radius="<<feature.edge_treatment.primary_size
                    <<" routes=";
                for(const auto& route:feature.edge_treatment.routes)
                    std::cout<<route.size()<<',';}
            if(feature.feature_kind==zima::document::FeatureKind::Flat)
                std::cout<<" attached="<<feature.flat.sheet_attachment;
            if(!feature.placement.references.empty())
                std::cout<<" source="<<feature.placement.references.front().owner_id<<":"
                    <<feature.placement.references.front().semantic_key;
            if(feature.feature_kind==zima::document::FeatureKind::Bend&&
                    feature.bend.sheet_attachment&&feature.placement.references.size()==3) {
                const auto& source=feature.placement.references.front();
                const auto edge=std::ranges::find_if(diagnostic_geometry.edges,
                    [&](const auto& candidate) {
                        return candidate.reference.owner_id==source.owner_id&&
                            candidate.reference.semantic_key==source.semantic_key&&
                            candidate.reference.instance_path==source.instance_path;
                    });
                const auto& anchor=feature.placement.references[2];
                std::cout<<" anchor="<<anchor.owner_id<<":"<<anchor.semantic_key;
                if(edge!=diagnostic_geometry.edges.end()) {
                    std::cout<<" endpoints=";
                    for(std::size_t endpoint=0;
                            endpoint<edge->edge_treatment_endpoint_references.size();++endpoint) {
                        const auto& reference=edge->edge_treatment_endpoint_references[endpoint];
                        std::cout<<'['<<reference.owner_id<<':'<<reference.semantic_key;
                        if(endpoint<edge->points.size())std::cout<<" @"<<edge->points[endpoint].x
                            <<','<<edge->points[endpoint].y<<','<<edge->points[endpoint].z;
                        std::cout<<']';
                    }
                } else std::cout<<" edge=missing";
            }
            if(feature.feature_kind==zima::document::FeatureKind::Flat&&
                    feature.flat.sheet_attachment&&!feature.placement.references.empty()) {
                const auto& source=feature.placement.references.front();
                const auto edge=std::ranges::find_if(diagnostic_geometry.edges,
                    [&](const auto& candidate) {
                        return candidate.reference.owner_id==source.owner_id&&
                            candidate.reference.semantic_key==source.semantic_key&&
                            candidate.reference.instance_path==source.instance_path;
                    });
                if(edge!=diagnostic_geometry.edges.end()) {
                    std::cout<<" roles=";
                    for(const auto& face:edge->edge_treatment_side_references)
                        std::cout<<static_cast<int>(face.sheet_role)<<',';
                } else std::cout<<" edge=missing";
            }
            std::cout<<'\n';
        }
        if(argc==3&&std::string_view(argv[2]).starts_with("cursor=")) {
            const auto cursor=static_cast<std::size_t>(std::stoull(
                std::string(std::string_view(argv[2]).substr(7))));
            check(cursor<=part.history.size(),"Requested diagnostic history cursor is unavailable");
            for(std::size_t index=cursor;index<part.history.size();++index)
                part.history[index].suppressed=true;
            part.set_history_cursor(cursor);
            zima::kernel::OcctKernel cold;
            const auto calculated=zima::workspace::calculate_part_with_resolved_references(
                cold,part);
            if(!calculated.empty())for(const auto& [owner,error]:
                    calculated.back().calculation_errors) {
                std::cout<<"calculation_error owner="<<owner<<" message="<<error<<'\n';
                const auto* feature=part.find_container(owner);
                if(feature&&feature->feature_kind==zima::document::FeatureKind::Flat) {
                    const auto sketch=std::ranges::find(part.sketches,
                        feature->flat.sketch_id,&zima::sketcher::Sketch::id);
                    if(sketch!=part.sketches.end()) {
                        for(const auto& reference:sketch->external_references)
                            if(reference.id.starts_with(sketch->id+":flat-attachment:"))
                                std::cout<<"  reference="<<reference.id<<" source="
                                    <<reference.source_semantic_key<<" point="
                                    <<(reference.cached_points.empty()?999:reference.cached_points.front()[0])
                                    <<','<<(reference.cached_points.empty()?999:reference.cached_points.front()[1])<<'\n';
                        for(const auto& constraint:sketch->constraints)
                            if(constraint.kind==zima::sketcher::ConstraintKind::PointReference) {
                                const auto* point=sketch->find_point(constraint.first_point_id);
                                std::cout<<"  binding="<<constraint.second_point_id<<" native="
                                    <<constraint.first_point_id<<" point="<<(point?point->x:999)
                                    <<','<<(point?point->y:999)<<'\n';
                            }
                    }
                }
            }
            check(!calculated.empty()&&calculated.back().calculation_errors.empty(),
                "Diagnostic history cursor failed");
            std::cout<<"cursor="<<cursor<<" regeneration=passed\n";
            return 0;
        }
        if(argc>=3) {
            const auto before=part.serialized();zima::kernel::OcctKernel kernel;
            if(std::filesystem::path(argv[2]).extension()==".prtz") {
                std::string_view selector=argc>=4?std::string_view(argv[3]):
                    std::string_view{};
                if(selector.starts_with("history=")) {
                    const auto separator=selector.find(',');
                    check(separator!=std::string_view::npos,
                        "History selector requires a face selector");
                    const auto retained=static_cast<std::size_t>(std::stoull(
                        std::string(selector.substr(8,separator-8))));
                    check(retained<=part.history.size()&&retained<=cache.size(),
                        "Requested source history boundary is unavailable");
                    while(part.history.size()>retained)
                        part.erase_history_object(part.history.back().id);
                    cache.resize(retained);
                    selector=selector.substr(separator+1);
                }
                const bool same_body=selector=="same-body"||
                    selector.starts_with("same-body-face=");
                std::string target;if(same_body)target=part.body_history.order().front();
                else for(const auto& body:part.body_history.bodies())if(body.entries.empty()){target=body.scope.id;break;}
                if(target.empty())target=part.body_history.create_body("Conversion verification");
                if(!target.empty()){part.body_history.move_body(target,part.body_history.order().size()-1);part.body_history.activate(target);}
                const auto& mesh=cache.back().body_outputs.at(part.body_history.order().front())->mesh;
                const auto& seed_faces=mesh.original_references.triangle_references;
                auto first=same_body?std::ranges::find_if(seed_faces,[&](const auto& face){
                    return face.surface&&face.surface->kind==zima::kernel::SurfaceGeometry::Kind::Plane&&
                        face.surface->axis.z<-.99&&!face.surface->reversed&&
                        std::ranges::find(mesh.triangle_references,face)!=
                            mesh.triangle_references.end();
                }):std::ranges::max_element(seed_faces,{},[](const auto& face){return face.surface&&face.surface->kind==zima::kernel::SurfaceGeometry::Kind::Plane?face.measured_area.value_or(0):0;});
                if(selector.starts_with("face=")||
                        selector.starts_with("same-body-face=")) {
                    std::map<std::string,const zima::kernel::FaceReference*> indexed;
                    std::set<std::string> current;
                    for(const auto& face:mesh.triangle_references)
                        current.insert(face.owner_id+":"+face.semantic_key);
                    for(const auto& face:seed_faces) {
                        const auto identity=face.owner_id+":"+face.semantic_key;
                        if(current.contains(identity))indexed.emplace(identity,&face);
                    }
                    const auto separator=selector.find('=');
                    const auto requested=static_cast<std::size_t>(std::stoull(
                        std::string(selector.substr(separator+1))));
                    check(requested<indexed.size(),"Requested diagnostic face is unavailable");
                    const auto selected=std::next(indexed.begin(),static_cast<std::ptrdiff_t>(requested))->second;
                    first=std::ranges::find(seed_faces,*selected);
                }
                check(first!=seed_faces.end(),"Fixture lacks the requested planar branch seed");
                const double thickness=argc==5?std::stod(argv[4]):same_body?
                    zima::document::sheet_metal_defaults(part).thickness_mm.value_or(1.):
                    zima::workspace::suggest_sheet_thickness(mesh,*first);
                std::cout<<"seed="<<first->semantic_key<<" thickness="<<thickness;
                if(first->surface)std::cout<<" kind="<<int(first->surface->kind)
                    <<" p="<<first->surface->origin.x<<','<<first->surface->origin.y<<','<<first->surface->origin.z
                    <<" axis="<<first->surface->axis.x<<','<<first->surface->axis.y<<','<<first->surface->axis.z
                    <<" reversed="<<first->surface->reversed;
                std::cout<<std::endl;
                const auto started=std::chrono::steady_clock::now();
                const auto converted=zima::workspace::prepare_sheet_from_body(part,cache,*first,thickness,kernel);
                converted.document.save(argv[2],converted.calculated);
                std::size_t lines=0,arcs=0,circles=0,splines=0;for(const auto& sketch:converted.document.sketches)if(std::ranges::find(converted.created,sketch.owner_container_id)!=converted.created.end()){lines+=sketch.segments.size();arcs+=sketch.arcs.size();circles+=sketch.circles.size();splines+=sketch.bsplines.size();}
                std::cout<<"Created="<<converted.created.size()<<" skipped="<<converted.skipped<<" lines="<<lines<<" arcs="<<arcs<<" circles="<<circles<<" splines="<<splines
                    <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<std::endl;
                auto reopened=zima::document::PartDocument::load(argv[2]);zima::kernel::OcctKernel cold;
                const auto regenerated=zima::workspace::calculate_part_with_resolved_references(cold,reopened);
                check(regenerated.back().calculation_errors.empty(),"Converted fixture failed cold regeneration");
                const auto volume=measured_volume(kernel,converted.calculated.back().body_outputs.at(target));
                near(measured_volume(cold,regenerated.back().body_outputs.at(target)),volume,.001);
                if(same_body) {
                    reopened.history.front().feature.sides[0].length+=10;
                    const auto independent=zima::workspace::calculate_part_with_resolved_references(cold,reopened);
                    near(measured_volume(cold,independent.back().body_outputs.at(target)),volume,.001);
                }
                std::cout<<std::setprecision(12)<<"source_volume="
                    <<measured_volume(kernel,cache.back().body_outputs.at(part.body_history.order().front()))
                    <<" converted_volume="<<volume<<" cold_regeneration=passed\n";
                return 0;
            }
            const auto result=zima::workspace::prepare_sheet_dxf(part,cache,kernel);
            if(before!=part.serialized())throw std::runtime_error("Sheet DXF changed its input document");
            zima::interchange::export_dxf(argv[2],result.contour,result.export_options);
            std::cout<<"Exported contour lines="<<result.contour.segments.size()<<" arcs="<<result.contour.arcs.size()<<" circles="<<result.contour.circles.size()<<" splines="<<result.contour.bsplines.size()<<" volume="<<result.volume<<'\n';return 0;
        }
        const auto& mesh=cache.back().mesh;
        const auto& packet=mesh;
        const auto face_key=[](const auto& face){return face.owner_id+":"+face.semantic_key;};
        std::map<std::string,zima::kernel::FaceReference> faces;
        for(const auto& face:packet.triangle_references)faces.emplace(face_key(face),face);
        for(const auto& face:packet.original_references.triangle_references)
            if(faces.contains(face_key(face)))faces[face_key(face)]=face;
        std::map<std::string,std::size_t> face_indices;
        std::size_t face_index=0;
        for(const auto& [key,face]:faces) {
            face_indices.emplace(key,face_index);
            std::cout<<"face["<<face_index++<<"] key="<<face.semantic_key;
            if(face.surface){const auto& s=*face.surface;std::cout<<" kind="<<int(s.kind)<<" r="<<s.radius<<" p="<<s.origin.x<<","<<s.origin.y<<","<<s.origin.z<<" axis="<<s.axis.x<<","<<s.axis.y<<","<<s.axis.z<<" reversed="<<s.reversed;}
            if(face.measured_area)std::cout<<" area="<<*face.measured_area;
            std::cout<<'\n';
        }
        std::cout<<"edges="<<packet.edges.size()<<'\n';
        if(std::getenv("ZIMA_CPP_SHEET_FRAGMENT_PROFILE")) {
            std::size_t fragment_index{};
            for(const auto& face:
                    packet.original_references.triangle_references) {
                std::cout<<"fragment["<<fragment_index++<<"] key="
                    <<face.semantic_key;
                if(face.surface) {
                    const auto& s=*face.surface;
                    std::cout<<" kind="<<int(s.kind)
                        <<" r="<<s.radius
                        <<" p="<<s.origin.x<<','<<s.origin.y<<','
                        <<s.origin.z
                        <<" axis="<<s.axis.x<<','<<s.axis.y<<','
                        <<s.axis.z
                        <<" reversed="<<s.reversed;
                }
                std::cout<<'\n';
            }
        }
        std::size_t adjacent=0;for(const auto& edge:mesh.edges)if(!edge.edge_treatment_side_references.empty())++adjacent;
        std::cout<<"body edges="<<mesh.edges.size()<<" with adjacency="<<adjacent<<'\n';
        const auto surface_normal=[](const zima::kernel::FaceReference& face,const zima::kernel::Vec3& at) {
            auto result=face.surface->axis;
            if(face.surface->kind==zima::kernel::SurfaceGeometry::Kind::Cylinder) {
                const auto delta=zima::kernel::sheet_material::sub(at,face.surface->origin);
                result=zima::kernel::sheet_material::unit(zima::kernel::sheet_material::sub(delta,
                    zima::kernel::sheet_material::mul(face.surface->axis,
                        zima::kernel::sheet_material::dot(delta,face.surface->axis))));
            }
            return zima::kernel::sheet_material::mul(result,face.surface->reversed?-1:1);
        };
        std::size_t edge_index=0;
        for(const auto& edge:mesh.edges) {
            if(edge.edge_treatment_side_references.empty())continue;
            std::cout<<"edge["<<edge_index++<<"] faces=";
            for(const auto& face:edge.edge_treatment_side_references) {
                const auto found=face_indices.find(face_key(face));
                if(found!=face_indices.end())std::cout<<found->second<<' ';
                else std::cout<<"? ";
            }
            if(edge.points.size()>=2)std::cout<<" a="<<edge.points.front().x<<','<<edge.points.front().y<<','<<edge.points.front().z
                <<" b="<<edge.points.back().x<<','<<edge.points.back().y<<','<<edge.points.back().z;
            if(edge.points.size()>=2&&edge.edge_treatment_side_references.size()==2&&
                    edge.edge_treatment_side_references[0].surface&&edge.edge_treatment_side_references[1].surface) {
                const auto at=edge.points[edge.points.size()/2];
                std::cout<<" tangent_dot="<<zima::kernel::sheet_material::dot(
                    surface_normal(edge.edge_treatment_side_references[0],at),
                    surface_normal(edge.edge_treatment_side_references[1],at));
            }
            std::cout<<'\n';
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
