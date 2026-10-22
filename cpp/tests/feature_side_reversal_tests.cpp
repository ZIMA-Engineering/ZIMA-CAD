#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/feature_direction.hpp>
#include <zima/document/feature_rotation_span.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/feature_rotation_direction.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <numbers>
#include <set>
#include <source_location>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <sstream>
#include <iomanip>

using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-6,
          std::source_location where=std::source_location::current()) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance)
        throw std::runtime_error(std::string(where.function_name())+":"+std::to_string(where.line())+
            " expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
document::PartDocument fixture(bool revolution,bool reverse_axis=false) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(4,-3,7,3));
    auto feature=document::PartDocument::create_feature_container(sketch.id);
    auto& p=feature.feature;p.origin_centerline=p.centroid_centerline=true;
    for(auto& side:p.sides)side.operation=revolution?document::FeatureSideOperation::Revolution:document::FeatureSideOperation::Extrusion;
    p.sides[0].length=12;p.sides[1].length=5;
    p.sides[0].angle_degrees=65;p.sides[1].angle_degrees=25;
    if(revolution) {
        const auto axis=reverse_axis?sketch.add_segment(0,10,0,-10):sketch.add_segment(0,-10,0,10);
        sketch.set_segment_centerline(axis,true);p.axis_segment_id=axis;
    }
    feature.value_locks={"side0_length","side0_angle"};
    sketch.owner_container_id=feature.id;
    auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Side reversal"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
    part.resolve_constructions();return part;
}
kernel::BodyResult calculate(const kernel::OcctKernel& engine,const document::PartDocument& part,bool external=false) {
    const auto calculated=engine.evaluate_history(part.kernel_operations(external));
    check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Reversed Feature failed calculation");
    return calculated.back();
}
void preview_extents(const document::PartDocument& part,double low,double high) {
    double minimum=1e100,maximum=-1e100;
    for(const auto& edge:part.feature_preview_edges(part.history.front()))for(const auto p:edge.points) {
        minimum=std::min(minimum,p.z);maximum=std::max(maximum,p.z);
    }
    near(minimum,low,2e-3);near(maximum,high,2e-3);
}
kernel::ModelEnvelope envelope(const kernel::ViewerMesh& mesh) {
    kernel::ModelEnvelope bounds;for(auto index:mesh.triangles)bounds.include(mesh.vertices.at(index));
    check(bounds.valid,"Feature has no calculated surface");return bounds;
}
std::set<std::string> cap_ids(const kernel::BodyResult& result) {
    std::set<std::string> ids;
    for(const auto& ref:result.mesh.original_references.triangle_references)
        if(ref.semantic_key.starts_with("start:from:")||ref.semantic_key.starts_with("end:from:"))ids.insert(ref.semantic_key);
    return ids;
}
void cap_locations(const kernel::BodyResult& result,const document::FeatureParameters& p,bool revolution) {
    const auto& refs=result.mesh.original_references;std::set<std::string> found;
    for(std::size_t i=0;i<refs.triangle_references.size();++i) {
        const auto& key=refs.triangle_references[i].semantic_key;
        const bool start=key.starts_with("start:from:"),end=key.starts_with("end:from:");
        if(!start&&!end)continue;
        const auto payload=std::string_view(key).substr(start?11:9);
        const auto separator=payload.find(':');
        if(separator==std::string_view::npos)continue;
        const auto parent=kernel::feature_side_parent(payload.substr(separator+1));
        if(!parent)continue;
        const auto side=parent->side==kernel::FeatureSide::End?0u:1u;
        // The near cap belongs to the opposite logical role. Only test each
        // side's far cap; both near caps intentionally stay on the Sketch.
        if(start!=(side==1))continue;
        const double amount=p.side_direction_sign(side)*(revolution?p.effective_side(side).angle_degrees:p.effective_side(side).length);
        for(unsigned vertex=0;vertex<3;++vertex) {
            const auto point=refs.vertices.at(refs.triangles.at(3*i+vertex));
            if(revolution)near(std::atan2(point.z,point.x)*180/std::numbers::pi,amount);
            else near(point.z,amount);
        }
        found.insert(key);
    }
    check(found.size()==2,"Logical Start/End far-cap ancestry disappeared");
}
void persistence_and_history(const kernel::OcctKernel& engine,document::PartDocument part,
                             const kernel::BodyResult& original) {
    document::DocumentSession session(part,{original});
    for(unsigned edit=0;edit<3;++edit) {
        auto candidate=session.document();candidate.history.front().feature.reversed_sides=!candidate.history.front().feature.reversed_sides;
        const auto result=calculate(engine,candidate);
        const auto before=session.document().serialized();
        session.commit(candidate,{result});
        check(session.undo()&&session.document().serialized()==before,"Direction reversal Undo changed the original side definition");
        check(session.redo()&&session.document().history.front().feature==candidate.history.front().feature,"Direction reversal Redo lost settings");
        std::vector<kernel::BodyResult> restored;
        const auto reopened=document::PartDocument::from_serialized(candidate.serialized({result}),&restored);
        check(reopened.serialized()==candidate.serialized()&&restored.size()==1,"Reversed native document lost authored data");
        near(calculate(engine,reopened).volume,result.volume);
        check(cap_ids(restored.back())==cap_ids(result),"Native cache lost reversed Start/End identities");
    }
}
void matrix(const kernel::OcctKernel& engine) {
    std::size_t cases{};
    for(bool revolution:{false,true})for(bool reverse_axis:{false,true}) {
        if(!revolution&&reverse_axis)continue;
        for(auto type:{document::ProfileResultType::Solid,document::ProfileResultType::Thin,document::ProfileResultType::Surface})
        for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}) {
            if(type!=document::ProfileResultType::Thin&&mode!=document::ThinMode::OneSide)continue;
            auto part=fixture(revolution,reverse_axis);auto& p=part.history.front().feature;
            p.result_type=type;p.thin_mode=mode;p.thin_thickness=.4;
            const auto original=calculate(engine,part);const auto initial=envelope(original.mesh);
            preview_extents(part,initial.minimum.z,initial.maximum.z);
            const auto sides=p.sides;const auto locks=part.history.front().value_locks;
            if(type!=document::ProfileResultType::Surface)cap_locations(original,p,revolution);
            else check(cap_ids(original).empty(),"Surface acquired filled caps");
            p.reversed_sides=true;const auto reversed=calculate(engine,part);const auto changed=envelope(reversed.mesh);
            preview_extents(part,changed.minimum.z,changed.maximum.z);
            near(changed.minimum.z,-initial.maximum.z,2e-3);near(changed.maximum.z,-initial.minimum.z,2e-3);
            near(changed.minimum.x,initial.minimum.x,2e-3);near(changed.maximum.x,initial.maximum.x,2e-3);
            near(changed.minimum.y,initial.minimum.y,2e-3);near(changed.maximum.y,initial.maximum.y,2e-3);
            near(reversed.volume,original.volume,1e-5);near(reversed.surface_area,original.surface_area,1e-5);
            check(p.sides==sides&&part.history.front().value_locks==locks,"Direction reversal moved panel definitions or locks");
            check(cap_ids(reversed)==cap_ids(original),"Direction reversal replaced logical cap references");
            if(type!=document::ProfileResultType::Surface)cap_locations(reversed,p,revolution);
            if(type==document::ProfileResultType::Solid) {
                if(revolution)near(original.volume,6*(49-16)*std::numbers::pi/4);
                else {near(initial.minimum.z,-5);near(initial.maximum.z,12);near(original.volume,18*17);}
                check(original.volume_integrals&&reversed.volume_integrals,"Missing exact volume integrals");
                near(reversed.volume_integrals->centroid.z,-original.volume_integrals->centroid.z,1e-5);
            }
            if(type==document::ProfileResultType::Solid&&!reverse_axis)persistence_and_history(engine,part,reversed);
            ++cases;
        }
    }
    std::cout<<"Side reversal geometry cases="<<cases<<" (Solid/Thin/Surface, all wall sides, both axis directions)\n";
}
void active_sides_and_draft(const kernel::OcctKernel& engine) {
    for(bool reversed:{false,true})for(bool symmetric:{false,true})for(unsigned active:{0u,1u,2u})for(double draft:{0.,4.}) {
        auto part=fixture(false);auto& p=part.history.front().feature;p.symmetric=symmetric;p.reversed_sides=reversed;
        if(active<2)p.sides[1-active].operation=document::FeatureSideOperation::None;
        p.sides[0].draft_angle_degrees=draft;p.sides[1].draft_angle_degrees=-draft;
        // A symmetric inactive first side intentionally has no model.
        if(symmetric&&active==1)continue;
        const auto result=calculate(engine,part);const auto bounds=envelope(result.mesh);
        const double first=active==1?0:12,second=symmetric?first:active==0?0:5;
        near(bounds.minimum.z,reversed?-first:-second);near(bounds.maximum.z,reversed?second:first);
        preview_extents(part,bounds.minimum.z,bounds.maximum.z);
        if(draft==0)near(result.volume,18*(first+second));
        const auto sides=p.sides;p.symmetric=!symmetric;p.symmetric=symmetric;
        check(p.sides==sides,"Symmetry overwrote independently stored side settings");
    }
    std::cout<<"Single/independent/symmetric extrusion and draft direction matrix passed\n";
}
void external_plane_limits(const kernel::OcctKernel& engine) {
    for(bool revolution:{false,true})for(bool reversed:{false,true})for(bool symmetric:{false,true}) {
        auto part=fixture(revolution);auto& p=part.history.front().feature;p.reversed_sides=reversed;p.symmetric=symmetric;
        for(std::size_t side=0;side<2;++side) {
            auto& settings=p.sides[side];settings.extrusion_extent=document::EndCondition::UpTo;
            settings.rotation_extent=document::FeatureRotationExtent::UpTo;
            document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Plane;
            target.reference={"external-plane",side?"second":"first","source-occurrence"};
            const double sign=p.side_direction_sign(side);
            if(revolution) {
                const double angle=sign*(side?25:65)*std::numbers::pi/180;
                target.fallback_normal={-std::sin(angle),0,std::cos(angle)};
            } else {target.fallback_origin={0,0,sign*(side?5:12)};target.fallback_normal={0,0,1};}
            settings.targets={target};
        }
        const auto immutable=p.sides;const auto result=calculate(engine,part,true);const auto bounds=envelope(result.mesh);
        if(!revolution) {
            const double second=symmetric?12:5;
            near(bounds.minimum.z,reversed?-12:-second);near(bounds.maximum.z,reversed?second:12);
            preview_extents(part,bounds.minimum.z,bounds.maximum.z);
        } else near(result.volume,6*(49-16)*(symmetric?130:90)*std::numbers::pi/360);
        check(p.sides==immutable,"Resolving reversed external limits wrote to source/panel definitions");
        // A captured external plane without a real owning Assembly is a
        // request fixture. Persist its definition, then resolve/calculate in
        // that external context again; do not stamp a context-free Part cache.
        const auto reopened=document::PartDocument::from_serialized(part.serialized());
        near(calculate(engine,reopened,true).volume,result.volume);
    }
    std::cout<<"External Plane Up To: both directions, symmetric/independent, native reopening passed\n";
}
void twist_and_mixed(const kernel::OcctKernel& engine) {
    for(bool reversed:{false,true})for(bool twist_reverse:{false,true})for(bool smooth:{false,true}) {
        auto part=fixture(false);auto& p=part.history.front().feature;p.reversed_sides=reversed;
        p.sides[0].operation=document::FeatureSideOperation::Twist;p.sides[0].angle_degrees=65;
        p.sides[0].twist_reverse=twist_reverse;p.sides[0].twist_smooth=smooth;p.sides[1].operation=document::FeatureSideOperation::None;
        const auto result=calculate(engine,part);const auto bounds=envelope(result.mesh);
        near(bounds.minimum.z,reversed?-12:0);near(bounds.maximum.z,reversed?0:12);
        preview_extents(part,bounds.minimum.z,bounds.maximum.z);
        const auto reopened=document::PartDocument::from_serialized(part.serialized({result}));
        check(reopened.history.front().feature==p,"Twist reverse or smooth choice changed on reopening");
        near(calculate(engine,reopened).volume,result.volume);
    }
    auto mixed=fixture(true);mixed.history.front().feature.sides[1].operation=document::FeatureSideOperation::Extrusion;
    const auto original=calculate(engine,mixed);mixed.history.front().feature.reversed_sides=true;
    const auto reversed=calculate(engine,mixed);near(reversed.volume,original.volume);
    near(reversed.volume_integrals->centroid.z,-original.volume_integrals->centroid.z);
    check(cap_ids(original)==cap_ids(reversed),"Mixed-operation reversal changed cap identity");
    std::cout<<"Twist travel, twist handedness/smoothing and mixed Extrusion/Revolution passed\n";
}
void through_all_cut(const kernel::OcctKernel& engine) {
    auto part=fixture(false);auto& stock=part.history.front();
    part.sketches.front()=sketcher::Sketch::create_default();auto& sketch=part.sketches.front();
    static_cast<void>(sketch.add_rectangle(-10,-10,10,10));sketch.owner_container_id=stock.id;
    stock.feature.sketch_id=sketch.id;stock.feature.symmetric=true;stock.feature.sides[0].length=30;
    auto cutter=sketcher::Sketch::create_default();static_cast<void>(cutter.add_circle(0,0,2));
    auto cut=document::PartDocument::create_feature_container(cutter.id);cutter.owner_container_id=cut.id;
    cut.combine_mode=document::CombineMode::Subtract;cut.feature.sides[0].extrusion_extent=document::EndCondition::ThroughAll;
    part.history.push_back(cut);part.sketches.push_back(cutter);auto graph=part.body_history;
    graph.insert({document::PartHistoryKind::Feature,cut.id});part.set_body_history(graph);part.resolve_constructions();
    const auto original=calculate(engine,part);part.history.back().feature.reversed_sides=true;
    const auto reversed=calculate(engine,part);const double removed=std::numbers::pi*4*30;
    near(original.volume,24000-removed);near(reversed.volume,original.volume);
    near(original.volume_integrals->centroid.z,-removed*15/(24000-removed));
    near(reversed.volume_integrals->centroid.z,-original.volume_integrals->centroid.z);
    check(cap_ids(original)==cap_ids(reversed),"Through All reversal changed logical references");
    std::cout<<"Through All cut: exact removed volume, opposite material half, stable references passed\n";
}
void native_points_and_axes(const kernel::OcctKernel& engine) {
    for(bool reversed:{false,true})for(bool axis:{false,true}) {
        auto part=fixture(false);auto& feature=part.history.front();auto& p=feature.feature;p.reversed_sides=reversed;
        if(axis)p.type=document::FeatureType::Axis;
        auto graph=part.body_history;graph.set_history_cursor(graph.active_body_id(),0);
        for(std::size_t side=0;side<2;++side) {
            auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
            point.origin=point.entity_origin={3,2,p.side_direction_sign(side)*(side?5:12)};
            document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Point;
            target.reference={point.container_origin.id,"point",{}};target.fallback_origin=point.origin;
            p.sides[side].targets={target};p.sides[side].extrusion_extent=document::EndCondition::UpTo;
            graph.insert({document::PartHistoryKind::Construction,point.id});part.constructions.push_back(point);
        }
        part.set_body_history(graph);part.resolve_constructions();
        const auto immutable=part.constructions;
        if(axis) {
            // set_body_history replaces the document and invalidates references
            // to its earlier history elements. Inspect the resolved definition.
            const auto& resolved=part.history.front();
            const auto mesh=part.feature_result_mesh(resolved);std::size_t count{};
            for(const auto& point:mesh.original_references.points) {
                if(!point.reference.semantic_key.starts_with("point:from:"))continue;
                const auto parent=kernel::feature_side_parent(point.reference.semantic_key.substr(11));
                if(!parent)continue;
                near(point.position.z,resolved.feature.side_direction_sign(0)*(parent->side==kernel::FeatureSide::End?12:-5));++count;
            }
            check(count==2,"Reversed datum Axis lost logical Start/End points");
        } else {
            const auto result=calculate(engine,part);near(result.volume,18*17);
            const auto bounds=envelope(result.mesh);near(bounds.minimum.z,reversed?-12:-5);near(bounds.maximum.z,reversed?5:12);
            preview_extents(part,bounds.minimum.z,bounds.maximum.z);
        }
        check(part.constructions==immutable,"Reversed native point limit modified source geometry");
    }
    for(bool reversed:{false,true}) {
        auto part=fixture(true);auto& p=part.history.front().feature;p.reversed_sides=reversed;
        p.sides[1].operation=document::FeatureSideOperation::None;p.sides[0].rotation_extent=document::FeatureRotationExtent::Full;
        const auto full=calculate(engine,part);near(full.volume,6*(49-16)*std::numbers::pi);
    }
    std::cout<<"Native Point Up To, datum Axis logical endpoints and full Revolution passed\n";
}
void rotation_end_contacts(const kernel::OcctKernel& engine) {
    for(bool reverse_axis:{false,true})for(bool reversed:{false,true})
    for(std::size_t side:{0u,1u})for(double other_angle:{25.,120.,180.,350.}) {
        auto part=fixture(true,reverse_axis);auto& p=part.history.front().feature;
        p.reversed_sides=reversed;p.sides[1-side].angle_degrees=other_angle;
        const auto other=p.sides[1-side];const auto locks=part.history.front().value_locks;
        const double limit=document::feature_rotation_drag_limit(p,side);
        check(document::set_feature_signed_angle(p,side,-limit),"Revolution contact angle rejected");
        check(p.sides[1-side]==other&&part.history.front().value_locks==locks&&p.reversed_sides!=reversed,"Revolution end contact moved the opposite panel or locks");
        const auto full=calculate(engine,part);near(full.volume,6*(49-16)*std::numbers::pi,1e-5);
        for(double gap_angle:{.1,.001}) {
            // A .001-degree gap at radius 4 is smaller than the default
            // .001-mm Boolean tolerance. Request the precision needed by that
            // fixture instead of changing product tolerance or weakening checks.
            if(gap_angle==.001)part.document_precision["linear_tolerance"]="0.0000001";
            p.sides[side].angle_degrees=limit-gap_angle;
            const auto gap=calculate(engine,part);
            const double expected=6*(49-16)*std::numbers::pi*(360-gap_angle)/360;
            const auto integrated=[&](auto&& self,const kernel::BodyResult& source)->double {
                if(source.kernel_shape==kernel::kernel_body_compound) {
                    double volume{};for(const auto& id:source.kernel_compound_bodies)volume+=self(self,source.body_outputs.at(id).get());return volume;
                }
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream input(source.kernel_shape);BRepTools::Read(shape,input,builder);
                check(!shape.IsNull(),"Cannot independently read rotation BRep");
                GProp_GProps adaptive;BRepGProp::VolumeProperties(shape,adaptive,1e-12);return adaptive.Mass();
            };
            near(integrated(integrated,gap),expected,1e-5);
            near(gap.volume,expected,1e-5);
        }
    }
    std::cout<<"32 double-Revolution end contacts and 64 gaps at requested precision: both sides, axis orientations and directions passed\n";
}
}
int main(){try{kernel::OcctKernel engine;matrix(engine);active_sides_and_draft(engine);external_plane_limits(engine);twist_and_mixed(engine);through_all_cut(engine);native_points_and_axes(engine);rotation_end_contacts(engine);return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
