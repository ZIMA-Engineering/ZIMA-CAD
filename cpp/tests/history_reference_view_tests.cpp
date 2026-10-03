#include <zima/document/part_document.hpp>
#include <zima/document/solid_state_reference_view.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::isfinite(a)&&std::abs(a-b)<1e-8,"Wrong boundary reference coordinate");}
kernel::ViewerReferenceGeometry plane(double x,std::string occurrence={}) {
    kernel::ViewerReferenceGeometry g;g.vertices={{x,0,0},{x,10,0},{x,0,10}};g.triangles={0,1,2};
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Plane;
    surface->origin={x,0,0};surface->axis={1,0,0};surface->radial={0,1,0};
    g.triangle_references={{"source","cap",occurrence,surface}};
    return g;
}
void verify(bool body,double offset,bool flip) {
    document::PartDocument doc;doc.document_id="reference-view-test";doc.name="Reference view";
    for(int i=0;i<3;++i) {
        sketcher::Sketch sketch;sketch.id="sketch-"+std::to_string(i);
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);
        feature.placement.references={{{},"source","cap",offset,flip}};
        feature.placement.references.front().supports_offset=true;
        feature.placement.references.front().flip=flip;
        sketch.owner_container_id=feature.id;doc.sketches.push_back(sketch);
        doc.history.push_back(feature);doc.history_order.push_back({document::PartHistoryKind::Feature,feature.id});
    }
    auto child=document::PartDocument::create_extrusion_container("unused");
    child.placement.references={{{},doc.history[1].id+":origin","origin:plane:yz",2,false}};
    child.placement.references.front().supports_offset=true;
    doc.history.push_back(child);doc.history_order.insert(doc.history_order.begin()+2,{document::PartHistoryKind::Feature,child.id});
    if(body) {
        document::BodyHistoryGraph graph;const auto id=graph.create_body("Body");
        for(const auto& entry:doc.history_order)graph.insert(entry);
        auto record=*graph.find(id);record.scope.placement.x=100;graph.update_body(record);
        doc.set_body_history(std::move(graph));
    }
    const auto original=plane(110);
    document::HistoryReferenceViews views;
    views[doc.history[1].id]={{{"source",""}},plane(120)};
    // A child attached to the preceding resolved Origin consumes that fresh
    // datum, while the third source-attached feature uses the original frame.
    doc.resolve_constructions(original,views);
    auto expected=[&](double x){document::Placement p;p.references=doc.history[0].placement.references;
        require(document::resolve_placement(p,plane(x)),"Baseline plane failed");return p.x-(body?100:0);};
    near(doc.history[0].placement.x,expected(110));near(doc.history[1].placement.x,expected(120));
    near(doc.history[2].placement.x,expected(110));
    require(doc.history[3].placement.reference_valid,"Descendant Origin was lost");
    near(doc.history[3].placement.x,doc.history[1].placement.x+2);
    for(int i=0;i<3;++i) {
        const auto& ref=doc.history[i].placement.references.front();
        require(ref.owner_id=="source"&&ref.semantic_key=="cap"&&ref.flip==flip&&
            std::signbit(ref.offset)==std::signbit(offset),"State view changed authored reference/side");
        near(doc.sketches[i].resolved_origin.x,doc.history[i].placement.x);
    }
    doc.resolve_constructions(original);
    near(doc.history[1].placement.x,expected(110));
    near(doc.history[3].placement.x,doc.history[1].placement.x+2);
    const auto saved=doc.serialized();
    views.begin()->second.geometry.triangles[0]=999;
    bool rejected=false;try{doc.resolve_constructions(original,views);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&doc.serialized()==saved,"Invalid reference view partially changed document");
}
void verify_occurrence_and_storage() {
    auto original=plane(10);document::history_reference_detail::append(original,plane(30,"other"));
    document::HistoryReferenceViews views{{"straight",{{{"source",""}},plane(20)}},
        {"missing",{{{"source",""}}, {}}}};
    document::HistoryReferenceResolver resolver(original,views);auto geometry=original;
    for(int i=0;i<100;++i) {
        resolver.enter("straight",geometry);
        require(geometry.vertices.size()==6&&geometry.triangle_references.size()==2,"State view accumulated mesh data");
        for(const auto& ref:geometry.triangle_references)near(ref.surface->origin.x,ref.instance_path.empty()?20:30);
        resolver.enter("missing",geometry);
        require(geometry.triangle_references.size()==1&&geometry.triangle_references[0].instance_path=="other",
            "Missing correspondence fell back to old geometry or removed another occurrence");
        resolver.enter("original",geometry);
        require(geometry.triangle_references.size()==2,"Original state did not return");
    }
    near(original.triangle_references.front().surface->origin.x,10);
}
void verify_correspondence() {
    auto original=plane(10),replacement=plane(20);
    document::history_reference_detail::append(replacement,plane(30,"other"));
    const std::vector<document::ConstructionReference> references{{{},"source","cap",-0.,true}};
    const auto view=document::solid_state_reference_view(original,replacement,{{"source",""}},references);
    require(view.geometry.vertices.size()==3 && view.geometry.triangle_references.size()==1,
        "Correspondence retained another occurrence or unused mesh vertices");
    near(view.geometry.triangle_references[0].surface->origin.x,20);
    require(std::signbit(references[0].offset),"Correspondence changed the stored side");
    const auto reject=[&](const kernel::ViewerReferenceGeometry& packet) {
        bool rejected=false;
        try{document::solid_state_reference_view(original,packet,{{"source",""}},references);}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid correspondence was accepted");
        near(original.vertices.front().x,10);
    };
    reject({}); // No fallback to the old curved frame.
    reject(plane(20,"other")); // Same owner/key in another occurrence is unrelated.
    auto wrong=plane(20);wrong.triangle_references[0].semantic_key="nearby-cap";reject(wrong);
    kernel::ViewerReferenceGeometry changed_kind;
    kernel::ViewerPoint point;point.position={20,0,0};point.reference={"source","cap"};
    changed_kind.points.push_back(point);reject(changed_kind);
    auto ambiguous=plane(20);ambiguous.points.push_back(point);reject(ambiguous);
    auto malformed=plane(20);malformed.triangles[0]=999;reject(malformed);
    // A selected face with several triangles still denotes exactly one face.
    auto triangulated=plane(20);triangulated.triangles.insert(triangulated.triangles.end(),{2,1,0});
    triangulated.triangle_references.push_back(triangulated.triangle_references[0]);
    require(document::solid_state_reference_view(original,triangulated,{{"source",""}},references)
        .geometry.triangle_references.size()==2,"Face triangulation was mistaken for ambiguous topology");
    // Missing geometry is only legal when no current downstream reference needs
    // it. The explicit empty override then prevents stale fallback in the resolver.
    const auto missing=document::solid_state_reference_view(original,{},{{"source",""}},{});
    require(missing.owners.size()==1 && missing.geometry.vertices.empty(),"Empty override lost owner scope");
    kernel::ViewerEdge edge;edge.reference={"source","rim"};edge.points={{10,0,0},{10,1,0}};
    original.edges.push_back(edge);edge.points={{20,0,0},{20,1,0}};replacement.edges.push_back(edge);
    point.reference.semantic_key="endpoint";point.position={10,0,0};original.points.push_back(point);
    point.position={20,0,0};replacement.points.push_back(point);
    kernel::ViewerAxis axis;axis.reference={"source","axis"};axis.point={10,0,0};original.axes.push_back(axis);
    axis.point={20,0,0};replacement.axes.push_back(axis);
    const std::vector<document::ConstructionReference> all_kinds{
        {{},"source","cap"},{{},"source","rim"},{{},"source","endpoint"},{{},"source","axis"}};
    const auto complete=document::solid_state_reference_view(original,replacement,{{"source",""}},all_kinds);
    require(complete.geometry.edges.size()==1 && complete.geometry.points.size()==1 &&
        complete.geometry.axes.size()==1,"Correspondence omitted a reference kind");
    near(complete.geometry.edges[0].points[0].x,20);
    near(complete.geometry.points[0].position.x,20);near(complete.geometry.axes[0].point.x,20);
    original.triangles[0]=999;
    bool invalid_original=false;
    try{document::solid_state_reference_view(original,replacement,{{"source",""}},all_kinds);}
    catch(const std::invalid_argument&){invalid_original=true;}
    require(invalid_original,"Malformed authored packet was accepted");
}
void verify_rotated_body() {
    document::PartDocument doc;doc.document_id="rotated-reference-view";
    document::BodyHistoryGraph graph;const auto id=graph.create_body("Rotated Body");
    for(int i=0;i<2;++i) {
        sketcher::Sketch sketch;sketch.id="rotated-sketch-"+std::to_string(i);
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);
        feature.placement.references={{{},"source","cap",2,true}};
        sketch.owner_container_id=feature.id;doc.sketches.push_back(sketch);
        doc.history.push_back(feature);graph.insert({document::PartHistoryKind::Feature,feature.id});
    }
    auto body=*graph.find(id);body.scope.placement.x=100;
    body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=90;
    graph.update_body(body);doc.set_body_history(std::move(graph));
    const auto original=plane(110);
    document::HistoryReferenceViews views{{doc.history[1].id,{{{"source",""}},plane(120)}}};
    doc.resolve_constructions(original,views);
    for(int i=0;i<2;++i) {
        const auto& p=doc.history[i].placement;
        require(p.reference_valid,"Rotated Body boundary reference was lost");
        near(p.x,0);near(p.y,-12-10*i);near(p.z,0);
        near(doc.sketches[i].resolved_origin.y,p.y);
    }
    doc.resolve_constructions(original);
    near(doc.history[1].placement.y,-12);
    near(doc.body_history.find(id)->scope.placement.rotation_z,90);
}
void verify_state_owned_identity_is_not_aliased() {
    for(int depth:{1,2})for(const std::string occurrence:{"","assembly/second"})
        for(bool flip:{false,true})for(double offset:{-0.,0.,2.}) {
        auto target=plane(20,occurrence),authored=plane(10,occurrence);
        kernel::ViewerEdge edge;edge.reference={"source","rim",occurrence};edge.points={{20,0,0},{20,1,0}};
        target.edges.push_back(edge);edge.points={{10,0,0},{10,1,0}};authored.edges.push_back(edge);
        kernel::ViewerPoint point;point.reference={"source","endpoint",occurrence};point.position={20,0,0};
        target.points.push_back(point);point.position={10,0,0};authored.points.push_back(point);
        kernel::ViewerAxis axis;axis.reference={"source","axis",occurrence};axis.point={20,0,0};
        target.axes.push_back(axis);axis.point={10,0,0};authored.axes.push_back(axis);
        const auto identify=[&](auto& ref) {
            for(int i=0;i<depth;++i) {
                ref.semantic_key=kernel::solid_state_child_key(ref.owner_id,ref.semantic_key);
                ref.owner_id="state-"+std::to_string(i);
            }
        };
        for(auto& ref:authored.triangle_references)identify(ref);
        for(auto& item:authored.edges)identify(item.reference);
        for(auto& item:authored.points)identify(item.reference);
        for(auto& item:authored.axes)identify(item.reference);
        std::vector<document::ConstructionReference> required;
        for(const auto* key:{"cap","rim","endpoint","axis"}) {
            document::ConstructionReference ref;ref.owner_id="source";ref.semantic_key=key;
            ref.instance_path=occurrence;ref.offset=offset;ref.flip=flip;identify(ref);required.push_back(ref);
        }
        const auto saved=required;
        // State-owned children use local material transport during explicit
        // calculation. The shared reference view must not invent source aliases.
        required.push_back(required.front());
        document::history_reference_detail::append(target,plane(99,"unrelated"));
        const auto view=document::solid_state_reference_view(authored,target,{{"source",occurrence}},required);
        require(!view.owners.contains({required.front().owner_id,occurrence}),
            "Shared reference view invented a state-owned alias");
        const auto& geometry=view.geometry;
        require(geometry.triangle_references.size()==1&&geometry.edges.size()==1&&
            geometry.points.size()==1&&geometry.axes.size()==1,"Original-source view lost or duplicated geometry");
        require(geometry.triangle_references.back().owner_id=="source"&&geometry.triangle_references.back().semantic_key=="cap",
            "Original-source view changed topology ownership");
        near(geometry.triangle_references.back().surface->origin.x,20);
        near(geometry.edges.back().points.front().x,20);
        near(geometry.points.back().position.x,20);near(geometry.axes.back().point.x,20);
        for(std::size_t i=0;i<saved.size();++i)require(required[i]==saved[i]&&
            std::signbit(required[i].offset)==std::signbit(offset),"State alias changed authored side or offset");
        near(authored.vertices.front().x,10);near(target.vertices.front().x,20);
    }
}
}
int main(){try {
    for(bool body:{false,true})for(bool flip:{false,true})for(double offset:{-2.,-0.,0.,3.})verify(body,offset,flip);
    verify_occurrence_and_storage();
    verify_rotated_body();
    verify_correspondence();
    verify_state_owned_identity_is_not_aliased();
    std::cout<<"History reference views preserve boundary frames, descendants, sides and occurrences\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
